#pragma once

// === Engine includes ===
#include <Commands.h>
#include <Engine.h>
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

        struct ScriptFieldDiagnostics final
        {
            uint32_t missingFieldCount = 0;
            uint32_t typeMismatchCount = 0;
            uint32_t orphanFieldCount = 0;
        };

        Inspector(Core::CQRS::Bridge* bridge, GameCore::GameWorld* a_gameWorld,
            GameCore::EntityId* a_selectedEntityId, Engine* a_engine)
            : editorBridge(bridge)
            , m_gameWorld(a_gameWorld)
            , m_selectedEntityId(a_selectedEntityId)
            , m_engine(a_engine)
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

            const std::vector<std::string>& registeredClasses =
                m_engine != nullptr
                ? m_engine->registered_script_classes()
                : get_empty_script_class_list();
            const bool isKnownClass = component->className.empty() ||
                (m_engine != nullptr &&
                    m_engine->has_registered_script_class(component->className));
            const std::vector<ECS::ScriptFieldValue>& defaultFieldValues =
                m_engine != nullptr
                ? m_engine->script_field_defaults(component->className)
                : get_empty_script_field_value_list();
            const char* previewValue = component->className.empty()
                ? "<empty>"
                : component->className.c_str();
            const bool hasClassName = !component->className.empty();
            const ScriptFieldDiagnostics fieldDiagnostics =
                diagnose_script_fields(*component, defaultFieldValues);
            const std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
                isKnownClass
                ? build_resolved_script_field_values(*component, defaultFieldValues)
                : component->fieldValues;

            if (ImGui::BeginCombo("className", previewValue))
            {
                if (ImGui::Selectable("<empty>", component->className.empty()))
                {
                    ECS::ScriptComponent nextComponent = *component;
                    nextComponent.className.clear();
                    nextComponent.fieldValues.clear();
                    submit_set_script_component_command(nextComponent);
                }

                for (const std::string& className : registeredClasses)
                {
                    const bool isSelected = component->className == className;
                    if (ImGui::Selectable(className.c_str(), isSelected))
                    {
                        ECS::ScriptComponent nextComponent = *component;
                        nextComponent.className = className;
                        nextComponent.fieldValues =
                            m_engine != nullptr
                            ? m_engine->script_field_defaults(className)
                            : std::vector<ECS::ScriptFieldValue>{};
                        submit_set_script_component_command(nextComponent);
                    }
                }

                ImGui::EndCombo();
            }

            if (registeredClasses.empty())
            {
                ImGui::TextUnformatted(
                    "登録済み Script クラスはありません。GameScript.dll を再ビルドしてください。");
            }
            else
            {
                ImGui::Text("registered: %u",
                    static_cast<uint32_t>(registeredClasses.size()));
            }

            if (!hasClassName)
            {
                ImGui::TextColored(ImVec4(0.90f, 0.80f, 0.35f, 1.0f),
                    "Script クラスが未選択です。");
            }
            else if (!isKnownClass)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
                    "未解決 Script: 現在の className は未登録です。");
                if (!component->fieldValues.empty())
                {
                    ImGui::TextWrapped(
                        "保存済み field 値を保持しています。Script を再ビルドして解決するか、別の className を選択してください。");
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
                    "解決済み Script");
            }

            bool isEnabled = component->isEnabled;
            if (ImGui::Checkbox("isEnabled", &isEnabled) &&
                isEnabled != component->isEnabled)
            {
                ECS::ScriptComponent nextComponent = *component;
                nextComponent.isEnabled = isEnabled;
                submit_set_script_component_command(nextComponent);
            }

            if (hasClassName)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("public fields");

                if (isKnownClass &&
                    (fieldDiagnostics.missingFieldCount > 0 ||
                        fieldDiagnostics.typeMismatchCount > 0 ||
                        fieldDiagnostics.orphanFieldCount > 0))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                        "field 定義との差分があります。");
                    if (fieldDiagnostics.missingFieldCount > 0)
                    {
                        ImGui::Text("missing: %u",
                            fieldDiagnostics.missingFieldCount);
                    }
                    if (fieldDiagnostics.typeMismatchCount > 0)
                    {
                        ImGui::Text("type mismatch: %u",
                            fieldDiagnostics.typeMismatchCount);
                    }
                    if (fieldDiagnostics.orphanFieldCount > 0)
                    {
                        ImGui::Text("orphan: %u",
                            fieldDiagnostics.orphanFieldCount);
                    }
                }

                if (isKnownClass && m_engine != nullptr &&
                    ImGui::Button("Reset Fields"))
                {
                    ECS::ScriptComponent nextComponent = *component;
                    nextComponent.fieldValues =
                        m_engine->script_field_defaults(component->className);
                    submit_set_script_component_command(nextComponent);
                }

                if (isKnownClass &&
                    (fieldDiagnostics.missingFieldCount > 0 ||
                        fieldDiagnostics.typeMismatchCount > 0 ||
                        fieldDiagnostics.orphanFieldCount > 0))
                {
                    ImGui::SameLine();
                    if (ImGui::Button("定義に合わせる"))
                    {
                        ECS::ScriptComponent nextComponent = *component;
                        nextComponent.fieldValues = resolvedFieldValues;
                        submit_set_script_component_command(nextComponent);
                    }
                }

                if (!isKnownClass)
                {
                    if (component->fieldValues.empty())
                    {
                        ImGui::TextUnformatted("保存済み field はありません。");
                    }
                    else
                    {
                        ImGui::BeginDisabled(true);
                        draw_script_field_editors(*component,
                            component->fieldValues);
                        ImGui::EndDisabled();
                    }
                }
                else if (resolvedFieldValues.empty())
                {
                    ImGui::TextUnformatted("public field はありません。");
                }
                else
                {
                    draw_script_field_editors(*component, resolvedFieldValues);
                }
            }
        }

        void draw_script_field_editors(
            const ECS::ScriptComponent& a_component,
            const std::vector<ECS::ScriptFieldValue>& a_fieldValues)
        {
            for (size_t fieldIndex = 0;
                fieldIndex < a_fieldValues.size();
                ++fieldIndex)
            {
                const ECS::ScriptFieldValue& fieldValue =
                    a_fieldValues[fieldIndex];
                ImGui::PushID(static_cast<int>(fieldIndex));

                switch (fieldValue.type)
                {
                case ECS::ScriptFieldType::Float:
                {
                    float value = fieldValue.floatValue;
                    if (ImGui::DragFloat(fieldValue.name.c_str(), &value, 0.01f))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        nextComponent.fieldValues = a_fieldValues;
                        nextComponent.fieldValues[fieldIndex].floatValue = value;
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::Int32:
                {
                    int value = fieldValue.intValue;
                    if (ImGui::DragInt(fieldValue.name.c_str(), &value, 1.0f))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        nextComponent.fieldValues = a_fieldValues;
                        nextComponent.fieldValues[fieldIndex].intValue = value;
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::Bool:
                {
                    bool value = fieldValue.boolValue;
                    if (ImGui::Checkbox(fieldValue.name.c_str(), &value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        nextComponent.fieldValues = a_fieldValues;
                        nextComponent.fieldValues[fieldIndex].boolValue = value;
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                }

                ImGui::PopID();
            }
        }

        [[nodiscard]] static int find_script_field_index(
            const std::vector<ECS::ScriptFieldValue>& a_fieldValues,
            const std::string& a_fieldName) noexcept
        {
            for (size_t fieldIndex = 0;
                fieldIndex < a_fieldValues.size();
                ++fieldIndex)
            {
                if (a_fieldValues[fieldIndex].name == a_fieldName)
                {
                    return static_cast<int>(fieldIndex);
                }
            }

            return -1;
        }

        [[nodiscard]] static std::vector<ECS::ScriptFieldValue>
            build_resolved_script_field_values(
                const ECS::ScriptComponent& a_component,
                const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues)
        {
            if (a_defaultFieldValues.empty())
            {
                return {};
            }

            std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
                a_defaultFieldValues;
            for (size_t defaultIndex = 0;
                defaultIndex < resolvedFieldValues.size();
                ++defaultIndex)
            {
                ECS::ScriptFieldValue& resolvedField =
                    resolvedFieldValues[defaultIndex];
                const int currentFieldIndex = find_script_field_index(
                    a_component.fieldValues,
                    resolvedField.name);
                if (currentFieldIndex < 0)
                {
                    continue;
                }

                const ECS::ScriptFieldValue& currentField =
                    a_component.fieldValues[static_cast<size_t>(currentFieldIndex)];
                if (currentField.type != resolvedField.type)
                {
                    continue;
                }

                resolvedField = currentField;
            }

            return resolvedFieldValues;
        }

        [[nodiscard]] static ScriptFieldDiagnostics diagnose_script_fields(
            const ECS::ScriptComponent& a_component,
            const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues) noexcept
        {
            ScriptFieldDiagnostics diagnostics{};

            for (const ECS::ScriptFieldValue& defaultField : a_defaultFieldValues)
            {
                const int currentFieldIndex = find_script_field_index(
                    a_component.fieldValues,
                    defaultField.name);
                if (currentFieldIndex < 0)
                {
                    ++diagnostics.missingFieldCount;
                    continue;
                }

                const ECS::ScriptFieldValue& currentField =
                    a_component.fieldValues[static_cast<size_t>(currentFieldIndex)];
                if (currentField.type != defaultField.type)
                {
                    ++diagnostics.typeMismatchCount;
                }
            }

            for (const ECS::ScriptFieldValue& currentField : a_component.fieldValues)
            {
                const int defaultFieldIndex = find_script_field_index(
                    a_defaultFieldValues,
                    currentField.name);
                if (defaultFieldIndex < 0)
                {
                    ++diagnostics.orphanFieldCount;
                }
            }

            return diagnostics;
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

        void submit_set_script_component_command(
            const ECS::ScriptComponent& a_component)
        {
            if (editorBridge == nullptr || m_selectedEntityId == nullptr ||
                *m_selectedEntityId == GameCore::k_invalidEntityId)
            {
                return;
            }

            Result result = editorBridge->submit_command(
                std::make_unique<SetScriptComponentCommand>(
                    *m_selectedEntityId, a_component));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit set ScriptComponent command: %s (code: %s, severity: %s) at %s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file,
                    result.line, result.function);
            }
        }

        [[nodiscard]] static const std::vector<std::string>&
            get_empty_script_class_list()
        {
            static const std::vector<std::string> k_empty{};
            return k_empty;
        }

        [[nodiscard]] static const std::vector<ECS::ScriptFieldValue>&
            get_empty_script_field_value_list()
        {
            static const std::vector<ECS::ScriptFieldValue> k_empty{};
            return k_empty;
        }

        Core::CQRS::Bridge* editorBridge = nullptr;
        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        Engine* m_engine = nullptr;
        GameCore::EntityId m_lastInspectedEntityId =
            GameCore::k_invalidEntityId;
        ComponentTab m_currentTab = ComponentTab::Base;
    };
}
