#pragma once

// === Base includes ===
#include <CueAssert.h>

// === Engine includes ===
#include <Commands.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class Hierarchy final
    {
    public:
        struct SceneEntry final
        {
            std::string name{};
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
            bool isPrimary = false;
        };

        struct ObjectEntry final
        {
            std::string name{};
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::EntityId parent = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
            std::vector<size_t> children{};
        };

        struct SceneNode final
        {
            SceneEntry scene{};
            std::vector<size_t> roots{};
        };

        struct DragObjectPayload final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
        };

        Hierarchy(Core::CQRS::Bridge* bridge, GameCore::GameWorld* gameWorld,
            GameCore::EntityId* a_selectedEntityId,
            GameCore::SceneId* a_selectedSceneId)
            : editorBridge(bridge)
            , m_gameWorld(gameWorld)
            , m_selectedEntityId(a_selectedEntityId)
            , m_selectedSceneId(a_selectedSceneId)
        {
        }
        ~Hierarchy() = default;

        void set_scenes(std::vector<SceneEntry> a_scenes)
        {
            m_sourceScenes = std::move(a_scenes);
        }

        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept
        {
            m_gameWorld = a_gameWorld;
        }

        void set_read_only(bool a_isReadOnly) noexcept
        {
            m_isReadOnly = a_isReadOnly;
            if (m_isReadOnly)
            {
                cancel_rename();
            }
        }

        void update()
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

            for (const SceneNode& scene : m_scenes)
            {
                draw_scene_node(scene);
            }

            ImGui::End();
        }

    private:
        [[nodiscard]] bool refresh_objects()
        {
            m_objects.clear();
            m_scenes.clear();
            m_objectIndexById.clear();

            size_t objectCount = 0;
            Result countResult = m_gameWorld->object_count(objectCount);
            if (countResult)
            {
                m_objects.reserve(objectCount);
                m_objectIndexById.reserve(objectCount);
            }

            const Result enumerateResult =
                m_gameWorld->for_each_object(
                    [this](GameCore::EntityId a_entityId,
                        GameCore::SceneId a_sceneId,
                        GameCore::GameObject& a_object)
                    {
                        std::string objectName{};
                        Result nameResult = a_object.name(objectName);
                        if (!nameResult || objectName.empty())
                        {
                            objectName = "GameObject";
                        }

                        GameCore::BaseComponent* base = nullptr;
                        GameCore::EntityId parent =
                            GameCore::k_invalidEntityId;
                        if (a_object.get_component(base) && base != nullptr)
                        {
                            if (base->tag == "EditorPreview")
                            {
                                return;
                            }
                            parent = base->parent;
                        }

                        m_objects.push_back(
                            { std::move(objectName), a_entityId, parent,
                                a_sceneId, {} });
                    });

            if (!enumerateResult)
            {
                return false;
            }

            std::sort(m_objects.begin(), m_objects.end(),
                [](const ObjectEntry& a_left, const ObjectEntry& a_right)
                {
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

            for (size_t objectIndex = 0; objectIndex < m_objects.size();
                 ++objectIndex)
            {
                m_objectIndexById.emplace(
                    m_objects[objectIndex].entityId, objectIndex);
            }

            for (const SceneEntry& scene : m_sourceScenes)
            {
                SceneNode node{};
                node.scene = scene;
                if (node.scene.name.empty())
                {
                    node.scene.name = "Scene";
                }
                m_scenes.push_back(std::move(node));
            }

            for (size_t objectIndex = 0; objectIndex < m_objects.size();
                 ++objectIndex)
            {
                ObjectEntry& object = m_objects[objectIndex];
                const auto parentIt = m_objectIndexById.find(object.parent);
                if (parentIt != m_objectIndexById.end() &&
                    m_objects[parentIt->second].sceneId == object.sceneId)
                {
                    m_objects[parentIt->second].children.push_back(objectIndex);
                    continue;
                }

                scene_node_for(object.sceneId).roots.push_back(objectIndex);
            }

            validate_selection();
            return true;
        }

        SceneNode& scene_node_for(GameCore::SceneId a_sceneId)
        {
            for (SceneNode& scene : m_scenes)
            {
                if (scene.scene.sceneId == a_sceneId)
                {
                    return scene;
                }
            }

            SceneNode node{};
            node.scene.sceneId = a_sceneId;
            node.scene.name = a_sceneId == GameCore::k_invalidSceneId
                ? "World"
                : "Scene";
            m_scenes.push_back(std::move(node));
            return m_scenes.back();
        }

        void validate_selection()
        {
            const bool hasSelectedObject =
                std::any_of(m_objects.begin(), m_objects.end(),
                    [this](const ObjectEntry& a_object)
                    {
                        return a_object.entityId == selected_entity_id();
                    });
            if (!hasSelectedObject)
            {
                set_selected_entity_id(GameCore::k_invalidEntityId);
            }

            const bool hasSelectedScene =
                std::any_of(m_scenes.begin(), m_scenes.end(),
                    [this](const SceneNode& a_scene)
                    {
                        return a_scene.scene.sceneId == selected_scene_id();
                    });
            if (!hasSelectedScene)
            {
                set_selected_scene_id(GameCore::k_invalidSceneId);
            }

            const bool hasRenamingObject =
                std::any_of(m_objects.begin(), m_objects.end(),
                    [this](const ObjectEntry& a_object)
                    {
                        return a_object.entityId == m_renamingEntityId;
                    });
            if (!hasRenamingObject)
            {
                cancel_rename();
            }
        }

        void draw_scene_node(const SceneNode& a_scene)
        {
            ImGui::PushID(
                static_cast<int>(a_scene.scene.sceneId & 0xffffffffu));

            const bool isSelected =
                selected_entity_id() == GameCore::k_invalidEntityId &&
                selected_scene_id() == a_scene.scene.sceneId;
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSelected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (a_scene.roots.empty())
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }

            const std::string label = a_scene.scene.name +
                (a_scene.scene.isPrimary ? " (Current)" : "");
            const bool isOpen = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked())
            {
                set_selected_scene_id(a_scene.scene.sceneId);
                set_selected_entity_id(GameCore::k_invalidEntityId);
            }

            draw_scene_drop_target(a_scene);

            if (isOpen)
            {
                for (const size_t objectIndex : a_scene.roots)
                {
                    draw_object_node(objectIndex);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        void draw_object_node(size_t a_objectIndex)
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
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
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
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                !m_isReadOnly)
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

        void draw_rename_input(const ObjectEntry& a_object)
        {
            if (m_focusRenameInput)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusRenameInput = false;
            }

            const bool submitted = ImGui::InputText("##Rename",
                m_renameBuffer.data(), m_renameBuffer.size(),
                ImGuiInputTextFlags_AutoSelectAll |
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool deactivatedAfterEdit =
                ImGui::IsItemDeactivatedAfterEdit();
            const bool deactivated = ImGui::IsItemDeactivated();
            const bool isActive = ImGui::IsItemActive();
            const bool isEscapePressed =
                isActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

            if (submitted || deactivatedAfterEdit)
            {
                submit_rename_command(a_object.entityId);
            }
            else if (isEscapePressed)
            {
                cancel_rename();
            }
            else if (deactivated)
            {
                cancel_rename();
            }
        }

        void draw_scene_drop_target(const SceneNode& a_scene)
        {
            if (m_isReadOnly)
            {
                return;
            }

            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(k_objectDragPayloadType);
                if (payload != nullptr &&
                    payload->DataSize == sizeof(DragObjectPayload))
                {
                    const DragObjectPayload& dragPayload =
                        *static_cast<const DragObjectPayload*>(payload->Data);
                    if (dragPayload.sceneId == a_scene.scene.sceneId)
                    {
                        submit_parent_command(
                            dragPayload.entityId,
                            GameCore::k_invalidEntityId);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        void draw_object_drag_source(const ObjectEntry& a_object)
        {
            if (m_isReadOnly)
            {
                return;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                DragObjectPayload payload{};
                payload.entityId = a_object.entityId;
                payload.sceneId = a_object.sceneId;
                ImGui::SetDragDropPayload(
                    k_objectDragPayloadType, &payload, sizeof(payload));
                ImGui::TextUnformatted(a_object.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        void draw_object_drop_target(const ObjectEntry& a_object)
        {
            if (m_isReadOnly)
            {
                return;
            }

            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(k_objectDragPayloadType);
                if (payload != nullptr &&
                    payload->DataSize == sizeof(DragObjectPayload))
                {
                    const DragObjectPayload& dragPayload =
                        *static_cast<const DragObjectPayload*>(payload->Data);
                    if (dragPayload.sceneId == a_object.sceneId &&
                        dragPayload.entityId != a_object.entityId)
                    {
                        submit_parent_command(
                            dragPayload.entityId,
                            a_object.entityId);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        void draw_object_context_menu(const ObjectEntry& a_object)
        {
            if (m_isReadOnly)
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

        void begin_rename(const ObjectEntry& a_object)
        {
            set_selected_entity_id(a_object.entityId);
            set_selected_scene_id(a_object.sceneId);
            m_renamingEntityId = a_object.entityId;
            m_focusRenameInput = true;
            std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
            const size_t copyLength =
                (std::min)(a_object.name.size(), m_renameBuffer.size() - 1);
            if (copyLength > 0)
            {
                std::memcpy(m_renameBuffer.data(), a_object.name.data(), copyLength);
            }
        }

        void cancel_rename()
        {
            m_renamingEntityId = GameCore::k_invalidEntityId;
            m_focusRenameInput = false;
            std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
        }

        void submit_rename_command(GameCore::EntityId a_entityId)
        {
            if (editorBridge == nullptr || m_isReadOnly)
            {
                cancel_rename();
                return;
            }

            std::string newName = m_renameBuffer.data();
            const auto objectIt = std::find_if(m_objects.begin(), m_objects.end(),
                [a_entityId](const ObjectEntry& a_object)
                {
                    return a_object.entityId == a_entityId;
                });

            if (objectIt != m_objects.end() && objectIt->name != newName)
            {
                Result result = editorBridge->submit_command(
                    std::make_unique<Cue::RenameObjectCommand>(a_entityId,
                        std::move(newName)));
                if (!result)
                {
                    CUE_ASSERTF(false,
                        "Failed to submit rename object command: %s (code: %s, severity: %s) at %s:%u in function %s",
                        result.message.data(), Cue::to_string(result.code),
                        Cue::to_string(result.severity), result.file,
                        result.line, result.function);
                }
            }

            cancel_rename();
        }

        void submit_delete_command(GameCore::EntityId a_entityId)
        {
            if (editorBridge == nullptr || m_isReadOnly)
            {
                return;
            }

            Result result = editorBridge->submit_command(
                std::make_unique<Cue::DeleteObjectCommand>(a_entityId));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit delete object command: %s (code: %s, severity: %s) at %s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file,
                    result.line, result.function);
            }

            if (selected_entity_id() == a_entityId)
            {
                set_selected_entity_id(GameCore::k_invalidEntityId);
            }
            if (m_renamingEntityId == a_entityId)
            {
                cancel_rename();
            }
        }

        void submit_parent_command(
            GameCore::EntityId a_entityId,
            GameCore::EntityId a_parentId)
        {
            if (editorBridge == nullptr || m_isReadOnly)
            {
                return;
            }

            Result result = editorBridge->submit_command(
                std::make_unique<Cue::SetParentCommand>(
                    a_entityId,
                    a_parentId,
                    true));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit set parent command: %s (code: %s, severity: %s) at %s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file,
                    result.line, result.function);
            }
        }

        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept
        {
            return m_selectedEntityId != nullptr
                ? *m_selectedEntityId
                : GameCore::k_invalidEntityId;
        }

        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept
        {
            if (m_selectedEntityId != nullptr)
            {
                *m_selectedEntityId = a_entityId;
            }
        }

        [[nodiscard]] GameCore::SceneId selected_scene_id() const noexcept
        {
            return m_selectedSceneId != nullptr
                ? *m_selectedSceneId
                : GameCore::k_invalidSceneId;
        }

        void set_selected_scene_id(GameCore::SceneId a_sceneId) noexcept
        {
            if (m_selectedSceneId != nullptr)
            {
                *m_selectedSceneId = a_sceneId;
            }
        }

        Core::CQRS::Bridge* editorBridge = nullptr;
        static constexpr const char* k_objectDragPayloadType =
            "CueHierarchyObject";
        GameCore::GameWorld* m_gameWorld = nullptr;
        std::vector<SceneEntry> m_sourceScenes{};
        std::vector<SceneNode> m_scenes{};
        std::vector<ObjectEntry> m_objects{};
        std::unordered_map<GameCore::EntityId, size_t> m_objectIndexById{};
        GameCore::EntityId* m_selectedEntityId = nullptr;
        GameCore::SceneId* m_selectedSceneId = nullptr;
        GameCore::EntityId m_renamingEntityId = GameCore::k_invalidEntityId;
        std::array<char, 256> m_renameBuffer{};
        bool m_focusRenameInput = false;
        bool m_isReadOnly = false;
    };
}
