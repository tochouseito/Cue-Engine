#include "Hierarchy.h"

// === Runtime includes ===
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    Hierarchy::Hierarchy(GameCore::GameWorld* a_gameWorld,
                         GameCore::EntityId* a_selectedEntityId,
                         GameCore::SceneId* a_selectedSceneId) noexcept
        : m_gameWorld(a_gameWorld),
          m_selectedEntityId(a_selectedEntityId),
          m_selectedSceneId(a_selectedSceneId)
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
        ImGuiTreeNodeFlags worldFlags =
            ImGuiTreeNodeFlags_DefaultOpen |
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

        const Result enumerateResult =
            m_gameWorld->for_each_object(
                [this](GameCore::EntityId a_entityId,
                       GameCore::GameObject a_object)
                {
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

        const bool hasSelectedObject =
            std::any_of(m_objects.begin(), m_objects.end(),
                [this](const ObjectEntry& a_object)
                {
                    return a_object.entityId == selected_entity_id();
                });
        if (!hasSelectedObject)
        {
            set_selected_entity_id(GameCore::k_invalidEntityId);
            set_selected_scene_id(GameCore::k_invalidSceneId);
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

    GameCore::EntityId Hierarchy::selected_entity_id() const noexcept
    {
        return m_selectedEntityId != nullptr
            ? *m_selectedEntityId
            : GameCore::k_invalidEntityId;
    }

    void Hierarchy::set_selected_entity_id(GameCore::EntityId a_entityId) noexcept
    {
        if (m_selectedEntityId != nullptr)
        {
            *m_selectedEntityId = a_entityId;
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
