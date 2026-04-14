#pragma once

// === Engine includes ===
#include <Commands.h>
#include <GameCore/Components.h>
#include <GameCore/GameWorld.h>

// === Editor includes ===
#include "Icon.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class Inspector final
    {
    public:
        enum class ComponentTab : uint8_t
        {
            Base,
            RenderableInfo,
            Transform,
            Camera,
            MeshFilter,
            StaticMeshRenderer,
            Script
        };

        struct ComponentTabEntry final
        {
            ComponentTab tab = ComponentTab::Base;
            const char* label = "";
        };

        struct AddableComponentEntry final
        {
            AddableComponentType type = AddableComponentType::Camera;
            const char* label = "";
        };

        Inspector(Core::CQRS::Bridge* bridge, GameCore::GameWorld* a_gameWorld,
            GameCore::EntityId* a_selectedEntityId)
            : editorBridge(bridge)
            , m_gameWorld(a_gameWorld)
            , m_selectedEntityId(a_selectedEntityId)
        {
        }
        ~Inspector() = default;

        void update()
        {
            ImGui::Begin("インスペクター");

            if (m_gameWorld == nullptr || m_selectedEntityId == nullptr)
            {
                ImGui::TextUnformatted("Inspector の依存が初期化されていません。");
                ImGui::End();
                return;
            }

            if (*m_selectedEntityId == GameCore::k_invalidEntityId)
            {
                ImGui::TextUnformatted(
                    "ヒエラルキーで GameObject を選択してください。");
                ImGui::End();
                return;
            }

            GameCore::GameObject object{};
            Result visitResult = m_gameWorld->visit_object(
                *m_selectedEntityId,
                [&object](GameCore::EntityId, GameCore::SceneId,
                    GameCore::GameObject& a_object)
                {
                    object = a_object;
                });
            if (!visitResult)
            {
                *m_selectedEntityId = GameCore::k_invalidEntityId;
                ImGui::TextUnformatted("選択中の GameObject は存在しません。");
                ImGui::End();
                return;
            }
            if (!object.is_valid())
            {
                *m_selectedEntityId = GameCore::k_invalidEntityId;
                ImGui::TextUnformatted("選択中の GameObject は無効です。");
                ImGui::End();
                return;
            }

            std::string objectName{};
            Result nameResult = object.name(objectName);
            if (!nameResult || objectName.empty())
            {
                objectName = "GameObject";
            }

            std::vector<ComponentTabEntry> componentTabs =
                collect_component_tabs(object);
            std::vector<AddableComponentEntry> addableComponents =
                collect_addable_components(object);
            sync_current_tab(*m_selectedEntityId, componentTabs);

            draw_tab_list(componentTabs, addableComponents);

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("InspectorContent", ImVec2(0.0f, 0.0f), true);
            ImGui::Text("Name: %s", objectName.c_str());
            ImGui::Text("EntityId: %u", *m_selectedEntityId);
            ImGui::Separator();
            draw_component_content(object);
            ImGui::EndChild();
            ImGui::End();
        }

    private:
        [[nodiscard]] std::vector<ComponentTabEntry> collect_component_tabs(
            const GameCore::GameObject& a_object) const
        {
            std::vector<ComponentTabEntry> tabs{};
            tabs.reserve(7);

            if (has_component<GameCore::BaseComponent>(a_object))
            {
                tabs.push_back({ ComponentTab::Base, "B" });
            }

            if (has_component<ECS::RenderableInfoComponent>(a_object))
            {
                tabs.push_back(
                    { ComponentTab::RenderableInfo, "R" });
            }

            if (has_component<ECS::TransformComponent>(a_object))
            {
                tabs.push_back({ ComponentTab::Transform, "T" });
            }

            if (has_component<ECS::CameraComponent>(a_object))
            {
                tabs.push_back({ ComponentTab::Camera, "C" });
            }

            if (has_component<ECS::MeshFilterComponent>(a_object))
            {
                tabs.push_back({ ComponentTab::MeshFilter, "M" });
            }

            if (has_component<ECS::StaticMeshRendererComponent>(a_object))
            {
                tabs.push_back(
                    { ComponentTab::StaticMeshRenderer, "S" });
            }

            if (has_component<ECS::ScriptComponent>(a_object))
            {
                tabs.push_back({ ComponentTab::Script, "Sc" });
            }

            return tabs;
        }

        [[nodiscard]] std::vector<AddableComponentEntry> collect_addable_components(
            const GameCore::GameObject& a_object) const
        {
            std::vector<AddableComponentEntry> components{};
            components.reserve(4);

            if (!has_component<ECS::CameraComponent>(a_object))
            {
                components.push_back(
                    { AddableComponentType::Camera, "CameraComponent" });
            }

            if (!has_component<ECS::MeshFilterComponent>(a_object))
            {
                components.push_back(
                    { AddableComponentType::MeshFilter, "MeshFilterComponent" });
            }

            if (!has_component<ECS::StaticMeshRendererComponent>(a_object))
            {
                components.push_back(
                    { AddableComponentType::StaticMeshRenderer,
                        "StaticMeshRendererComponent" });
            }

            if (!has_component<ECS::ScriptComponent>(a_object))
            {
                components.push_back(
                    { AddableComponentType::Script, "ScriptComponent" });
            }

            return components;
        }

        void sync_current_tab(GameCore::EntityId a_entityId,
            const std::vector<ComponentTabEntry>& a_componentTabs)
        {
            if (m_lastInspectedEntityId != a_entityId)
            {
                m_lastInspectedEntityId = a_entityId;
                if (!a_componentTabs.empty())
                {
                    m_currentTab = a_componentTabs.front().tab;
                }
                return;
            }

            const bool hasCurrentTab =
                std::any_of(a_componentTabs.begin(), a_componentTabs.end(),
                    [this](const ComponentTabEntry& a_entry)
                    {
                        return a_entry.tab == m_currentTab;
                    });
            if (!hasCurrentTab && !a_componentTabs.empty())
            {
                m_currentTab = a_componentTabs.front().tab;
            }
        }

        void draw_tab_list(
            const std::vector<ComponentTabEntry>& a_componentTabs,
            const std::vector<AddableComponentEntry>& a_addableComponents)
        {
            const float tabListWidth = 34.0f;
            ImGui::BeginChild("InspectorTabs", ImVec2(tabListWidth, 0.0f), true);

            for (const ComponentTabEntry& entry : a_componentTabs)
            {
                const bool isSelected = m_currentTab == entry.tab;
                if (ImGui::Selectable(entry.label, isSelected, 0,
                        ImVec2(0.0f, 32.0f)))
                {
                    m_currentTab = entry.tab;
                }
            }

            const float buttonHeight = 36.0f;
            const float buttonY = (std::max)(
                ImGui::GetCursorPosY(),
                ImGui::GetWindowHeight() - buttonHeight -
                    ImGui::GetStyle().WindowPadding.y);
            ImGui::SetCursorPosY(buttonY);

            ImGui::BeginDisabled(a_addableComponents.empty());
            if (ImGui::Button(
                    CUE_ICON_ADD, ImVec2(0.0f, buttonHeight)))
            {
                ImGui::OpenPopup("AddComponentPopup");
            }
            ImGui::EndDisabled();

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                if (a_addableComponents.empty())
                {
                    ImGui::TextUnformatted(
                        "追加できるコンポーネントはありません。");
                }
                else
                {
                    for (const AddableComponentEntry& entry : a_addableComponents)
                    {
                        if (ImGui::MenuItem(entry.label))
                        {
                            submit_add_component_command(entry.type);
                        }
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::EndChild();
        }

        void draw_component_content(GameCore::GameObject& a_object)
        {
            switch (m_currentTab)
            {
            case ComponentTab::Base:
                draw_base_component(a_object);
                break;

            case ComponentTab::RenderableInfo:
                draw_renderable_info_component(a_object);
                break;

            case ComponentTab::Transform:
                draw_transform_component(a_object);
                break;

            case ComponentTab::Camera:
                draw_camera_component(a_object);
                break;

            case ComponentTab::MeshFilter:
                draw_mesh_filter_component(a_object);
                break;

            case ComponentTab::StaticMeshRenderer:
                draw_static_mesh_renderer_component(a_object);
                break;

            case ComponentTab::Script:
                draw_script_component(a_object);
                break;
            }
        }

        void draw_base_component(GameCore::GameObject& a_object)
        {
            GameCore::BaseComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted("BaseComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("BaseComponent");
            ImGui::Separator();
            ImGui::Text("name: %s", component->name.c_str());
            ImGui::Text("tag: %s", component->tag.c_str());
            ImGui::Text("owningSceneId: %u", component->owningSceneId);
            ImGui::Text("parent: %u", component->parent);
            ImGui::Text("isActiveSelf: %s",
                component->isActiveSelf ? "true" : "false");
            ImGui::Text("isPersistent: %s",
                component->isPersistent ? "true" : "false");
        }

        void draw_renderable_info_component(GameCore::GameObject& a_object)
        {
            ECS::RenderableInfoComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted(
                    "RenderableInfoComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("RenderableInfoComponent");
            ImGui::Separator();
            draw_renderable_id_text("objectId", component->objectId);
            draw_renderable_id_text("transformId", component->transformId);
        }

        void draw_transform_component(GameCore::GameObject& a_object)
        {
            ECS::TransformComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted("TransformComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("TransformComponent");
            ImGui::Separator();
            draw_float3_text("position", component->position);
            draw_float3_text("rotation", component->rotation);
            draw_float3_text("scale", component->scale);
        }

        void draw_camera_component(GameCore::GameObject& a_object)
        {
            ECS::CameraComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted("CameraComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("CameraComponent");
            ImGui::Separator();
            ImGui::Text("isMain: %s", component->isMain ? "true" : "false");
            ImGui::Text("fovY: %.3f", component->fovY);
            ImGui::Text("aspectRatio: %.3f", component->aspectRatio);
            ImGui::Text("nearZ: %.3f", component->nearZ);
            ImGui::Text("farZ: %.3f", component->farZ);
        }

        void draw_mesh_filter_component(GameCore::GameObject& a_object)
        {
            ECS::MeshFilterComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted(
                    "MeshFilterComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("MeshFilterComponent");
            ImGui::Separator();
            if (component->meshId == ECS::k_invalidMeshId)
            {
                ImGui::TextUnformatted("meshId: invalid");
            }
            else
            {
                ImGui::Text("meshId: %u", component->meshId);
            }
        }

        void draw_static_mesh_renderer_component(GameCore::GameObject& a_object)
        {
            ECS::StaticMeshRendererComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted(
                    "StaticMeshRendererComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("StaticMeshRendererComponent");
            ImGui::Separator();
            ImGui::Text("materialId: %u", component->materialId);
            ImGui::Text("visible: %s", component->visible ? "true" : "false");
        }

        void draw_script_component(GameCore::GameObject& a_object)
        {
            ECS::ScriptComponent* component = nullptr;
            if (!a_object.get_component(component) || component == nullptr)
            {
                ImGui::TextUnformatted("ScriptComponent が見つかりません。");
                return;
            }

            ImGui::TextUnformatted("ScriptComponent");
            ImGui::Separator();
            ImGui::Text("className: %s",
                component->className.empty() ? "<empty>" : component->className.c_str());
            ImGui::Text("isEnabled: %s",
                component->isEnabled ? "true" : "false");
        }

        void draw_float3_text(const char* a_label, const Math::float3& a_value)
        {
            ImGui::Text("%s: (%.3f, %.3f, %.3f)", a_label, a_value.x, a_value.y,
                a_value.z);
        }

        void draw_renderable_id_text(const char* a_label, uint32_t a_value)
        {
            if (a_value == ECS::k_invalidRenderableId)
            {
                ImGui::Text("%s: invalid", a_label);
                return;
            }

            ImGui::Text("%s: %u", a_label, a_value);
        }

        template <typename T>
        [[nodiscard]] bool has_component(
            const GameCore::GameObject& a_object) const
        {
            bool hasComponent = false;
            Result result = a_object.has_component<T>(hasComponent);
            return result && hasComponent;
        }

        void submit_add_component_command(AddableComponentType a_type)
        {
            if (editorBridge == nullptr || m_selectedEntityId == nullptr ||
                *m_selectedEntityId == GameCore::k_invalidEntityId)
            {
                return;
            }

            Result result = editorBridge->submit_command(
                std::make_unique<AddComponentCommand>(
                    *m_selectedEntityId, a_type));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit add component command: %s (code: %s, severity: %s) at %s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file,
                    result.line, result.function);
            }
        }

        Core::CQRS::Bridge* editorBridge = nullptr;
        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        GameCore::EntityId m_lastInspectedEntityId =
            GameCore::k_invalidEntityId;
        ComponentTab m_currentTab = ComponentTab::Base;
    };
}
