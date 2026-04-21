#pragma once

// === Engine includes ===
#include <Commands.h>
#include <Engine.h>
#include <GameCore/Components.h>
#include <GameCore/GameWorld.h>
#include <Script/MarionnetteObject.h>

// === Editor includes ===
#include "Icon.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cstddef>
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

        struct ObjectReferenceEntry final
        {
            std::string label{};
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
        };

        struct ScriptReferenceFieldPair final
        {
            std::string baseName{};
            const MarionnetteProperty* entityProperty = nullptr;
            const MarionnetteProperty* classProperty = nullptr;
            const ECS::ScriptFieldValue* entityFieldValue = nullptr;
            const ECS::ScriptFieldValue* classFieldValue = nullptr;
            uint32_t entityPropertyIndex = 0;
            uint32_t classPropertyIndex = 0;
        };

        struct ScriptReferenceResolution final
        {
            enum class Kind : uint8_t
            {
                Empty,
                MissingEntity,
                MissingClass,
                UnknownClass,
                TargetMissingScript,
                ClassMismatch,
                Resolved,
            };

            std::string targetName{};
            std::string currentClassName{};
            Kind kind = Kind::Empty;
        };

        struct TargetScriptReferenceState final
        {
            bool hasObject = false;
            bool hasScriptComponent = false;
            std::string targetName{};
            ECS::ScriptComponent scriptComponent{};
        };

        struct ScriptFieldDiagnostics final
        {
            uint32_t missingFieldCount = 0;
            uint32_t typeMismatchCount = 0;
            uint32_t orphanFieldCount = 0;
            uint32_t storageMismatchCount = 0;
            uint32_t unresolvedClassReferenceCount = 0;
        };

        struct ScriptSchemaFieldStatus final
        {
            enum class Kind : uint8_t
            {
                Ok,
                Missing,
                TypeMismatch,
                StorageMismatch,
                UnresolvedClassReference,
                Orphan,
            };

            std::string fieldName{};
            std::string referencedClassName{};
            std::string currentTypeName{};
            std::string expectedTypeName{};
            std::string currentStorageName{};
            std::string expectedStorageName{};
            Kind kind = Kind::Ok;
        };

        enum class ScriptFieldStorage : uint8_t
        {
            Serialized,
            Transient,
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
            const MarionnetteClass* marionnetteClass =
                m_engine != nullptr
                ? m_engine->find_marionnette_class(component->className)
                : nullptr;
            const std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
                isKnownClass
                ? build_resolved_script_field_values(
                    *component,
                    defaultFieldValues,
                    marionnetteClass)
                : collect_script_field_values(*component);
            const char* previewValue = component->className.empty()
                ? "<empty>"
                : component->className.c_str();
            const bool hasClassName = !component->className.empty();
            const ScriptFieldDiagnostics fieldDiagnostics =
                diagnose_script_fields(
                    *component,
                    defaultFieldValues,
                    marionnetteClass);
            const std::vector<ScriptSchemaFieldStatus> schemaFieldStatuses =
                marionnetteClass != nullptr
                ? build_script_schema_field_statuses(
                    *component,
                    defaultFieldValues,
                    *marionnetteClass)
                : std::vector<ScriptSchemaFieldStatus>{};
            if (ImGui::BeginCombo("className", previewValue))
            {
                if (ImGui::Selectable("<empty>", component->className.empty()))
                {
                    ECS::ScriptComponent nextComponent = *component;
                    nextComponent.className.clear();
                    nextComponent.serializedFieldValues.clear();
                    nextComponent.transientFieldValues.clear();
                    submit_set_script_component_command(nextComponent);
                }

                for (const std::string& className : registeredClasses)
                {
                    const bool isSelected = component->className == className;
                    if (ImGui::Selectable(className.c_str(), isSelected))
                    {
                        ECS::ScriptComponent nextComponent = *component;
                        nextComponent.className = className;
                        const std::vector<ECS::ScriptFieldValue> nextDefaults =
                            m_engine != nullptr
                            ? m_engine->script_field_defaults(className)
                            : std::vector<ECS::ScriptFieldValue>{};
                        const MarionnetteClass* nextMarionnetteClass =
                            m_engine != nullptr
                            ? m_engine->find_marionnette_class(className)
                            : nullptr;
                        if (nextMarionnetteClass != nullptr)
                        {
                            assign_script_field_values_to_component(
                                nextComponent,
                                *nextMarionnetteClass,
                                nextDefaults);
                        }
                        else
                        {
                            nextComponent.serializedFieldValues = nextDefaults;
                            nextComponent.transientFieldValues.clear();
                        }
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
                if (!component->serializedFieldValues.empty())
                {
                    ImGui::TextWrapped(
                        "保存済み field 値を保持しています。Script を再ビルドして解決するか、別の className を選択してください。");
                }
                else if (!component->transientFieldValues.empty())
                {
                    ImGui::TextWrapped(
                        "一時 field 値を保持しています。Script を再ビルドして解決するか、別の className を選択してください。");
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
                    "解決済み Script");
                if (marionnetteClass != nullptr)
                {
                    ImGui::Text("schema: %s / properties: %u",
                        marionnetteClass->name != nullptr
                            ? marionnetteClass->name
                            : "<unnamed>",
                        marionnetteClass->propertyCount);
                }
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
                        fieldDiagnostics.orphanFieldCount > 0 ||
                        fieldDiagnostics.storageMismatchCount > 0 ||
                        fieldDiagnostics.unresolvedClassReferenceCount > 0))
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
                    if (fieldDiagnostics.storageMismatchCount > 0)
                    {
                        ImGui::Text("storage mismatch: %u",
                            fieldDiagnostics.storageMismatchCount);
                    }
                    if (fieldDiagnostics.unresolvedClassReferenceCount > 0)
                    {
                        ImGui::Text("unresolved class ref: %u",
                            fieldDiagnostics.unresolvedClassReferenceCount);
                    }
                }

                if (isKnownClass && m_engine != nullptr &&
                    ImGui::Button("Reset Fields"))
                {
                    ECS::ScriptComponent nextComponent = *component;
                    const std::vector<ECS::ScriptFieldValue> nextDefaults =
                        m_engine->script_field_defaults(component->className);
                    if (marionnetteClass != nullptr)
                    {
                        assign_script_field_values_to_component(
                            nextComponent,
                            *marionnetteClass,
                            nextDefaults);
                    }
                    else
                    {
                        nextComponent.serializedFieldValues = nextDefaults;
                        nextComponent.transientFieldValues.clear();
                    }
                    submit_set_script_component_command(nextComponent);
                }

                if (isKnownClass &&
                    (fieldDiagnostics.missingFieldCount > 0 ||
                        fieldDiagnostics.typeMismatchCount > 0 ||
                        fieldDiagnostics.orphanFieldCount > 0 ||
                        fieldDiagnostics.storageMismatchCount > 0 ||
                        fieldDiagnostics.unresolvedClassReferenceCount > 0))
                {
                    ImGui::SameLine();
                    if (ImGui::Button("定義に合わせる"))
                    {
                        ECS::ScriptComponent nextComponent = *component;
                        if (marionnetteClass != nullptr)
                        {
                            assign_script_field_values_to_component(
                                nextComponent,
                                *marionnetteClass,
                                resolvedFieldValues);
                        }
                        else
                        {
                            nextComponent.serializedFieldValues =
                                resolvedFieldValues;
                            nextComponent.transientFieldValues.clear();
                        }
                        submit_set_script_component_command(nextComponent);
                    }
                }

                if (isKnownClass && marionnetteClass != nullptr)
                {
                    draw_script_schema_statuses(
                        *component,
                        *marionnetteClass,
                        defaultFieldValues,
                        schemaFieldStatuses);
                }

                if (!isKnownClass)
                {
                    const std::vector<ECS::ScriptFieldValue> currentFieldValues =
                        collect_script_field_values(*component);
                    if (currentFieldValues.empty())
                    {
                        ImGui::TextUnformatted("保存済み field はありません。");
                    }
                    else
                    {
                        ImGui::BeginDisabled(true);
                        draw_script_field_editors(*component,
                            currentFieldValues);
                        ImGui::EndDisabled();
                    }
                }
                else if (marionnetteClass != nullptr &&
                    marionnetteClass->propertyCount == 0)
                {
                    ImGui::TextUnformatted("public field はありません。");
                }
                else if (marionnetteClass != nullptr)
                {
                    draw_script_field_editors(
                        *component,
                        *marionnetteClass,
                        resolvedFieldValues);
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
            const MarionnetteClass& a_marionnetteClass,
            const std::vector<ECS::ScriptFieldValue>& a_fieldValues)
        {
            std::vector<bool> consumedProperties(
                a_marionnetteClass.propertyCount,
                false);
            for (uint32_t propertyIndex = 0;
                propertyIndex < a_marionnetteClass.propertyCount;
                ++propertyIndex)
            {
                if (consumedProperties[propertyIndex])
                {
                    continue;
                }

                const MarionnetteProperty& property =
                    a_marionnetteClass.properties[propertyIndex];
                if (property.name == nullptr)
                {
                    continue;
                }

                ScriptReferenceFieldPair scriptReferencePair{};
                if (try_build_script_reference_field_pair(
                        a_marionnetteClass,
                        a_fieldValues,
                        propertyIndex,
                        scriptReferencePair))
                {
                    consumedProperties[scriptReferencePair.entityPropertyIndex] = true;
                    consumedProperties[scriptReferencePair.classPropertyIndex] = true;
                    draw_script_reference_field_editor(
                        a_component,
                        scriptReferencePair);
                    continue;
                }

                const int fieldIndex = find_script_field_index(
                    a_fieldValues, property.name);
                if (fieldIndex < 0)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                        "%s: schema に存在しますが値がありません。",
                        property.name);
                    continue;
                }

                const ECS::ScriptFieldValue& fieldValue =
                    a_fieldValues[static_cast<size_t>(fieldIndex)];
                const bool isReadOnly = has_any_flags(
                    property.flags,
                    MarionnettePropertyFlag_ReadOnly);
                const ScriptFieldStorage storage =
                    get_script_field_storage(property);

                ImGui::PushID(static_cast<int>(propertyIndex));
                if (isReadOnly)
                {
                    ImGui::BeginDisabled(true);
                }

                switch (fieldValue.type)
                {
                case ECS::ScriptFieldType::Float:
                {
                    float value = fieldValue.floatValue;
                    if (ImGui::DragFloat(property.name, &value, 0.01f))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = fieldValue;
                        nextFieldValue.floatValue = value;
                        set_script_field_value(
                            nextComponent,
                            storage,
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::Int32:
                {
                    int value = fieldValue.intValue;
                    if (ImGui::DragInt(property.name, &value, 1.0f))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = fieldValue;
                        nextFieldValue.intValue = value;
                        set_script_field_value(
                            nextComponent,
                            storage,
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::Bool:
                {
                    bool value = fieldValue.boolValue;
                    if (ImGui::Checkbox(property.name, &value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = fieldValue;
                        nextFieldValue.boolValue = value;
                        set_script_field_value(
                            nextComponent,
                            storage,
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::EntityRef:
                {
                    GameCore::EntityId value = fieldValue.entityValue;
                    if (draw_entity_reference_editor(property.name, value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = fieldValue;
                        nextFieldValue.entityValue = value;
                        set_script_field_value(
                            nextComponent,
                            storage,
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::ClassRef:
                {
                    std::string value = fieldValue.classValue;
                    if (draw_script_class_reference_editor(property.name, value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = fieldValue;
                        nextFieldValue.classValue = std::move(value);
                        set_script_field_value(
                            nextComponent,
                            storage,
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                }

                if (isReadOnly)
                {
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextUnformatted("ReadOnly");
                }

                ImGui::PopID();
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
                        nextComponent.serializedFieldValues = a_fieldValues;
                        nextComponent.serializedFieldValues[fieldIndex].floatValue = value;
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
                        nextComponent.serializedFieldValues = a_fieldValues;
                        nextComponent.serializedFieldValues[fieldIndex].intValue = value;
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
                        nextComponent.serializedFieldValues = a_fieldValues;
                        nextComponent.serializedFieldValues[fieldIndex].boolValue = value;
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::EntityRef:
                {
                    GameCore::EntityId value = fieldValue.entityValue;
                    if (draw_entity_reference_editor(fieldValue.name.c_str(), value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        nextComponent.serializedFieldValues = a_fieldValues;
                        nextComponent.serializedFieldValues[fieldIndex].entityValue =
                            value;
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                case ECS::ScriptFieldType::ClassRef:
                {
                    std::string value = fieldValue.classValue;
                    if (draw_script_class_reference_editor(
                            fieldValue.name.c_str(),
                            value))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        nextComponent.serializedFieldValues = a_fieldValues;
                        nextComponent.serializedFieldValues[fieldIndex].classValue =
                            std::move(value);
                        submit_set_script_component_command(nextComponent);
                    }
                    break;
                }
                }

                ImGui::PopID();
            }
        }

        [[nodiscard]] std::vector<ObjectReferenceEntry>
            collect_object_reference_entries() const
        {
            std::vector<ObjectReferenceEntry> entries{};
            if (m_gameWorld == nullptr)
            {
                return entries;
            }

            size_t objectCount = 0;
            Result countResult = m_gameWorld->object_count(objectCount);
            if (countResult)
            {
                entries.reserve(objectCount);
            }

            const Result iterateResult = m_gameWorld->for_each_object(
                [&entries](GameCore::EntityId a_entityId, GameCore::SceneId,
                    GameCore::GameObject& a_object)
                {
                    std::string objectName{};
                    Result nameResult = a_object.name(objectName);
                    if (!nameResult || objectName.empty())
                    {
                        objectName = "GameObject";
                    }

                    entries.push_back({
                        objectName + " (" + std::to_string(a_entityId) + ")",
                        a_entityId
                    });
                });
            if (!iterateResult)
            {
                entries.clear();
                return entries;
            }

            std::sort(entries.begin(), entries.end(),
                [](const ObjectReferenceEntry& a_left,
                    const ObjectReferenceEntry& a_right)
                {
                    if (a_left.label == a_right.label)
                    {
                        return a_left.entityId < a_right.entityId;
                    }

                    return a_left.label < a_right.label;
                });
            return entries;
        }

        [[nodiscard]] bool draw_entity_reference_editor(
            const char* a_label,
            GameCore::EntityId& a_inOutEntityId) const
        {
            const std::vector<ObjectReferenceEntry> entries =
                collect_object_reference_entries();
            const auto currentIt = std::find_if(entries.begin(), entries.end(),
                [&a_inOutEntityId](const ObjectReferenceEntry& a_entry)
                {
                    return a_entry.entityId == a_inOutEntityId;
                });
            const std::string previewText = currentIt != entries.end()
                ? currentIt->label
                : (a_inOutEntityId == GameCore::k_invalidEntityId
                    ? std::string("<empty>")
                    : std::to_string(a_inOutEntityId));

            bool changed = false;
            if (ImGui::BeginCombo(a_label, previewText.c_str()))
            {
                if (ImGui::Selectable(
                        "<empty>",
                        a_inOutEntityId == GameCore::k_invalidEntityId))
                {
                    a_inOutEntityId = GameCore::k_invalidEntityId;
                    changed = true;
                }

                for (const ObjectReferenceEntry& entry : entries)
                {
                    if (ImGui::Selectable(
                            entry.label.c_str(),
                            entry.entityId == a_inOutEntityId))
                    {
                        a_inOutEntityId = entry.entityId;
                        changed = true;
                    }
                }

                ImGui::EndCombo();
            }

            if (m_selectedEntityId != nullptr &&
                *m_selectedEntityId != GameCore::k_invalidEntityId)
            {
                ImGui::SameLine();
                if (ImGui::Button("選択中を設定"))
                {
                    a_inOutEntityId = *m_selectedEntityId;
                    changed = true;
                }
            }

            return changed;
        }

        [[nodiscard]] bool draw_script_class_reference_editor(
            const char* a_label,
            std::string& a_inOutClassName) const
        {
            const std::vector<std::string>& registeredClasses =
                m_engine != nullptr
                ? m_engine->registered_script_classes()
                : get_empty_script_class_list();
            const char* previewValue = a_inOutClassName.empty()
                ? "<empty>"
                : a_inOutClassName.c_str();

            bool changed = false;
            if (ImGui::BeginCombo(a_label, previewValue))
            {
                if (ImGui::Selectable("<empty>", a_inOutClassName.empty()))
                {
                    a_inOutClassName.clear();
                    changed = true;
                }

                for (const std::string& className : registeredClasses)
                {
                    if (ImGui::Selectable(
                            className.c_str(),
                            className == a_inOutClassName))
                    {
                        a_inOutClassName = className;
                        changed = true;
                    }
                }

                ImGui::EndCombo();
            }

            return changed;
        }

        [[nodiscard]] ScriptReferenceResolution resolve_script_reference(
            GameCore::EntityId a_entityId,
            std::string_view a_className) const
        {
            ScriptReferenceResolution resolution{};
            if (a_entityId == GameCore::k_invalidEntityId &&
                a_className.empty())
            {
                resolution.kind = ScriptReferenceResolution::Kind::Empty;
                return resolution;
            }
            if (a_entityId == GameCore::k_invalidEntityId)
            {
                resolution.kind = ScriptReferenceResolution::Kind::MissingEntity;
                return resolution;
            }
            if (a_className.empty())
            {
                resolution.kind = ScriptReferenceResolution::Kind::MissingClass;
                return resolution;
            }
            if (m_engine == nullptr ||
                !m_engine->has_registered_script_class(a_className))
            {
                resolution.kind = ScriptReferenceResolution::Kind::UnknownClass;
                return resolution;
            }
            if (m_gameWorld == nullptr)
            {
                resolution.kind = ScriptReferenceResolution::Kind::MissingEntity;
                return resolution;
            }

            GameCore::GameObject object{};
            Result objectResult = m_gameWorld->find_object(a_entityId, object);
            if (!objectResult || !object.is_valid())
            {
                resolution.kind = ScriptReferenceResolution::Kind::MissingEntity;
                return resolution;
            }

            std::string objectName{};
            Result nameResult = object.name(objectName);
            if (nameResult && !objectName.empty())
            {
                resolution.targetName = std::move(objectName);
            }

            ECS::ScriptComponent* scriptComponent = nullptr;
            Result scriptResult = object.get_component(scriptComponent);
            if (!scriptResult || scriptComponent == nullptr)
            {
                resolution.kind = ScriptReferenceResolution::Kind::TargetMissingScript;
                return resolution;
            }

            resolution.currentClassName = scriptComponent->className;
            if (scriptComponent->className != a_className)
            {
                resolution.kind = ScriptReferenceResolution::Kind::ClassMismatch;
                return resolution;
            }

            resolution.kind = ScriptReferenceResolution::Kind::Resolved;
            return resolution;
        }

        [[nodiscard]] TargetScriptReferenceState query_target_script_reference_state(
            GameCore::EntityId a_entityId) const
        {
            TargetScriptReferenceState state{};
            if (m_gameWorld == nullptr ||
                a_entityId == GameCore::k_invalidEntityId)
            {
                return state;
            }

            GameCore::GameObject object{};
            Result objectResult = m_gameWorld->find_object(a_entityId, object);
            if (!objectResult || !object.is_valid())
            {
                return state;
            }

            state.hasObject = true;
            (void)object.name(state.targetName);

            ECS::ScriptComponent* scriptComponent = nullptr;
            Result scriptResult = object.get_component(scriptComponent);
            if (!scriptResult || scriptComponent == nullptr)
            {
                return state;
            }

            state.hasScriptComponent = true;
            state.scriptComponent = *scriptComponent;
            return state;
        }

        void draw_script_reference_field_editor(
            const ECS::ScriptComponent& a_component,
            const ScriptReferenceFieldPair& a_pair)
        {
            if (a_pair.entityProperty == nullptr ||
                a_pair.classProperty == nullptr ||
                a_pair.entityFieldValue == nullptr ||
                a_pair.classFieldValue == nullptr)
            {
                return;
            }

            const bool isReadOnly = has_any_flags(
                    a_pair.entityProperty->flags,
                    MarionnettePropertyFlag_ReadOnly) ||
                has_any_flags(
                    a_pair.classProperty->flags,
                    MarionnettePropertyFlag_ReadOnly);
            const std::string label =
                format_script_reference_label(a_pair.baseName);

            ImGui::PushID(static_cast<int>(a_pair.entityPropertyIndex));
            ImGui::TextUnformatted(label.c_str());
            if (isReadOnly)
            {
                ImGui::BeginDisabled(true);
            }

            GameCore::EntityId entityValue = a_pair.entityFieldValue->entityValue;
            if (draw_entity_reference_editor("Entity", entityValue))
            {
                ECS::ScriptComponent nextComponent = a_component;
                ECS::ScriptFieldValue nextFieldValue = *a_pair.entityFieldValue;
                nextFieldValue.entityValue = entityValue;
                set_script_field_value(
                    nextComponent,
                    get_script_field_storage(*a_pair.entityProperty),
                    nextFieldValue);
                submit_set_script_component_command(nextComponent);
            }

            std::string classValue = a_pair.classFieldValue->classValue;
            if (draw_script_class_reference_editor("Class", classValue))
            {
                ECS::ScriptComponent nextComponent = a_component;
                ECS::ScriptFieldValue nextFieldValue = *a_pair.classFieldValue;
                nextFieldValue.classValue = std::move(classValue);
                set_script_field_value(
                    nextComponent,
                    get_script_field_storage(*a_pair.classProperty),
                    nextFieldValue);
                submit_set_script_component_command(nextComponent);
            }

            const ScriptReferenceResolution resolution =
                resolve_script_reference(entityValue, classValue);
            draw_script_reference_resolution(resolution);
            draw_script_reference_resolution_actions(
                a_component,
                a_pair,
                entityValue,
                classValue,
                resolution);

            if (isReadOnly)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("ReadOnly");
            }

            ImGui::PopID();
        }

        void draw_script_reference_resolution(
            const ScriptReferenceResolution& a_resolution) const
        {
            ImVec4 color = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
            const char* label = "Empty";
            std::string detail{};

            switch (a_resolution.kind)
            {
            case ScriptReferenceResolution::Kind::Empty:
                label = "Empty";
                detail = "entity と class が未設定です。";
                break;
            case ScriptReferenceResolution::Kind::MissingEntity:
                color = ImVec4(1.0f, 0.80f, 0.35f, 1.0f);
                label = "MissingEntity";
                detail = "entity が未設定か存在しません。";
                break;
            case ScriptReferenceResolution::Kind::MissingClass:
                color = ImVec4(1.0f, 0.80f, 0.35f, 1.0f);
                label = "MissingClass";
                detail = "class が未設定です。";
                break;
            case ScriptReferenceResolution::Kind::UnknownClass:
                color = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
                label = "UnknownClass";
                detail = "class が未登録です。";
                break;
            case ScriptReferenceResolution::Kind::TargetMissingScript:
                color = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
                label = "TargetMissingScript";
                detail = "target entity に ScriptComponent がありません。";
                break;
            case ScriptReferenceResolution::Kind::ClassMismatch:
                color = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
                label = "ClassMismatch";
                detail = "target entity の className と一致しません。";
                break;
            case ScriptReferenceResolution::Kind::Resolved:
                color = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
                label = "Resolved";
                detail = "script reference は解決されています。";
                break;
            }

            ImGui::TextColored(color, "[%s]", label);
            ImGui::SameLine();
            ImGui::TextUnformatted(detail.c_str());
            if (!a_resolution.targetName.empty())
            {
                ImGui::Text("target: %s", a_resolution.targetName.c_str());
            }
            if (!a_resolution.currentClassName.empty() &&
                a_resolution.kind == ScriptReferenceResolution::Kind::ClassMismatch)
            {
                ImGui::Text("target className: %s",
                    a_resolution.currentClassName.c_str());
            }
        }

        void draw_script_reference_resolution_actions(
            const ECS::ScriptComponent& a_component,
            const ScriptReferenceFieldPair& a_pair,
            GameCore::EntityId a_entityId,
            std::string_view a_className,
            const ScriptReferenceResolution& a_resolution)
        {
            if (a_pair.entityProperty == nullptr ||
                a_pair.classProperty == nullptr)
            {
                return;
            }

            const TargetScriptReferenceState targetState =
                query_target_script_reference_state(a_entityId);
            switch (a_resolution.kind)
            {
            case ScriptReferenceResolution::Kind::MissingClass:
            case ScriptReferenceResolution::Kind::UnknownClass:
            case ScriptReferenceResolution::Kind::ClassMismatch:
            {
                if (targetState.hasScriptComponent &&
                    !targetState.scriptComponent.className.empty())
                {
                    if (ImGui::Button("target class を採用"))
                    {
                        ECS::ScriptComponent nextComponent = a_component;
                        ECS::ScriptFieldValue nextFieldValue = *a_pair.classFieldValue;
                        nextFieldValue.classValue =
                            targetState.scriptComponent.className;
                        set_script_field_value(
                            nextComponent,
                            get_script_field_storage(*a_pair.classProperty),
                            nextFieldValue);
                        submit_set_script_component_command(nextComponent);
                    }
                }

                if (a_resolution.kind == ScriptReferenceResolution::Kind::ClassMismatch &&
                    !a_className.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("target に現在 class を適用"))
                    {
                        ECS::ScriptComponent nextTargetComponent =
                            targetState.scriptComponent;
                        nextTargetComponent.className = std::string(a_className);
                        const std::vector<ECS::ScriptFieldValue> nextDefaults =
                            m_engine != nullptr
                            ? m_engine->script_field_defaults(a_className)
                            : std::vector<ECS::ScriptFieldValue>{};
                        const MarionnetteClass* nextMarionnetteClass =
                            m_engine != nullptr
                            ? m_engine->find_marionnette_class(a_className)
                            : nullptr;
                        if (nextMarionnetteClass != nullptr)
                        {
                            assign_script_field_values_to_component(
                                nextTargetComponent,
                                *nextMarionnetteClass,
                                nextDefaults);
                        }
                        else
                        {
                            nextTargetComponent.serializedFieldValues =
                                nextDefaults;
                            nextTargetComponent.transientFieldValues.clear();
                        }

                        submit_set_script_component_command(
                            a_entityId,
                            nextTargetComponent);
                    }
                }
                break;
            }

            case ScriptReferenceResolution::Kind::TargetMissingScript:
            {
                if (m_engine != nullptr &&
                    !a_className.empty() &&
                    m_engine->has_registered_script_class(a_className) &&
                    ImGui::Button("target に ScriptComponent を作成"))
                {
                    ECS::ScriptComponent nextTargetComponent{};
                    nextTargetComponent.className = std::string(a_className);
                    nextTargetComponent.isEnabled = true;
                    const std::vector<ECS::ScriptFieldValue> nextDefaults =
                        m_engine->script_field_defaults(a_className);
                    const MarionnetteClass* nextMarionnetteClass =
                        m_engine->find_marionnette_class(a_className);
                    if (nextMarionnetteClass != nullptr)
                    {
                        assign_script_field_values_to_component(
                            nextTargetComponent,
                            *nextMarionnetteClass,
                            nextDefaults);
                    }
                    else
                    {
                        nextTargetComponent.serializedFieldValues = nextDefaults;
                    }

                    submit_set_script_component_command(
                        a_entityId,
                        nextTargetComponent);
                }
                break;
            }

            case ScriptReferenceResolution::Kind::Resolved:
            case ScriptReferenceResolution::Kind::Empty:
            case ScriptReferenceResolution::Kind::MissingEntity:
                break;
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

        [[nodiscard]] static ScriptFieldStorage get_script_field_storage(
            const MarionnetteProperty& a_property) noexcept
        {
            return has_any_flags(
                a_property.flags,
                MarionnettePropertyFlag_Serialize)
                ? ScriptFieldStorage::Serialized
                : ScriptFieldStorage::Transient;
        }

        [[nodiscard]] static std::vector<ECS::ScriptFieldValue>&
            get_script_field_storage_values(
                ECS::ScriptComponent& a_component,
                ScriptFieldStorage a_storage) noexcept
        {
            return a_storage == ScriptFieldStorage::Serialized
                ? a_component.serializedFieldValues
                : a_component.transientFieldValues;
        }

        [[nodiscard]] static const std::vector<ECS::ScriptFieldValue>&
            get_script_field_storage_values(
                const ECS::ScriptComponent& a_component,
                ScriptFieldStorage a_storage) noexcept
        {
            return a_storage == ScriptFieldStorage::Serialized
                ? a_component.serializedFieldValues
                : a_component.transientFieldValues;
        }

        static void erase_script_field_value(
            std::vector<ECS::ScriptFieldValue>& a_fieldValues,
            const std::string& a_fieldName)
        {
            const int fieldIndex =
                find_script_field_index(a_fieldValues, a_fieldName);
            if (fieldIndex < 0)
            {
                return;
            }

            a_fieldValues.erase(a_fieldValues.begin() +
                static_cast<std::ptrdiff_t>(fieldIndex));
        }

        static void upsert_script_field_value(
            std::vector<ECS::ScriptFieldValue>& a_fieldValues,
            const ECS::ScriptFieldValue& a_fieldValue)
        {
            const int fieldIndex =
                find_script_field_index(a_fieldValues, a_fieldValue.name);
            if (fieldIndex >= 0)
            {
                a_fieldValues[static_cast<size_t>(fieldIndex)] = a_fieldValue;
                return;
            }

            a_fieldValues.push_back(a_fieldValue);
        }

        static void set_script_field_value(
            ECS::ScriptComponent& a_component,
            ScriptFieldStorage a_storage,
            const ECS::ScriptFieldValue& a_fieldValue)
        {
            erase_script_field_value(
                a_component.serializedFieldValues,
                a_fieldValue.name);
            erase_script_field_value(
                a_component.transientFieldValues,
                a_fieldValue.name);
            upsert_script_field_value(
                get_script_field_storage_values(a_component, a_storage),
                a_fieldValue);
        }

        [[nodiscard]] static const ECS::ScriptFieldValue*
            find_script_field_value(
                const ECS::ScriptComponent& a_component,
                const std::string& a_fieldName,
                ScriptFieldStorage a_preferredStorage,
                ScriptFieldStorage* a_outStorage = nullptr) noexcept
        {
            const std::vector<ECS::ScriptFieldValue>& preferredValues =
                get_script_field_storage_values(a_component, a_preferredStorage);
            const int preferredIndex =
                find_script_field_index(preferredValues, a_fieldName);
            if (preferredIndex >= 0)
            {
                if (a_outStorage != nullptr)
                {
                    *a_outStorage = a_preferredStorage;
                }

                return &preferredValues[static_cast<size_t>(preferredIndex)];
            }

            const ScriptFieldStorage fallbackStorage =
                a_preferredStorage == ScriptFieldStorage::Serialized
                ? ScriptFieldStorage::Transient
                : ScriptFieldStorage::Serialized;
            const std::vector<ECS::ScriptFieldValue>& fallbackValues =
                get_script_field_storage_values(a_component, fallbackStorage);
            const int fallbackIndex =
                find_script_field_index(fallbackValues, a_fieldName);
            if (fallbackIndex >= 0)
            {
                if (a_outStorage != nullptr)
                {
                    *a_outStorage = fallbackStorage;
                }

                return &fallbackValues[static_cast<size_t>(fallbackIndex)];
            }

            return nullptr;
        }

        [[nodiscard]] static std::vector<ECS::ScriptFieldValue>
            collect_script_field_values(const ECS::ScriptComponent& a_component)
        {
            std::vector<ECS::ScriptFieldValue> fieldValues =
                a_component.serializedFieldValues;
            fieldValues.reserve(
                a_component.serializedFieldValues.size() +
                a_component.transientFieldValues.size());
            for (const ECS::ScriptFieldValue& fieldValue :
                a_component.transientFieldValues)
            {
                if (find_script_field_index(fieldValues, fieldValue.name) >= 0)
                {
                    continue;
                }

                fieldValues.push_back(fieldValue);
            }

            return fieldValues;
        }

        [[nodiscard]] static std::string format_script_reference_label(
            std::string_view a_baseName)
        {
            if (a_baseName.empty())
            {
                return "ScriptRef";
            }

            return std::string(a_baseName) + "ScriptRef";
        }

        [[nodiscard]] static bool try_build_script_reference_field_pair(
            const MarionnetteClass& a_marionnetteClass,
            const std::vector<ECS::ScriptFieldValue>& a_fieldValues,
            uint32_t a_propertyIndex,
            ScriptReferenceFieldPair& a_outPair)
        {
            a_outPair = {};
            if (a_propertyIndex >= a_marionnetteClass.propertyCount)
            {
                return false;
            }

            const MarionnetteProperty& property =
                a_marionnetteClass.properties[a_propertyIndex];
            if (property.name == nullptr)
            {
                return false;
            }

            std::string entityFieldName{};
            std::string classFieldName{};
            std::string baseName{};
            uint32_t entityPropertyIndex = a_propertyIndex;
            uint32_t classPropertyIndex = a_propertyIndex;

            const std::string_view propertyName = property.name;
            const std::string_view propertyGroupName =
                property.groupName != nullptr
                ? std::string_view(property.groupName)
                : std::string_view{};
            if (!propertyGroupName.empty() &&
                property.referenceRole != MarionnettePropertyReferenceRole::None)
            {
                baseName.assign(propertyGroupName);
                entityFieldName = baseName + "Entity";
                classFieldName = baseName + "ScriptClass";

                if (property.referenceRole ==
                    MarionnettePropertyReferenceRole::ScriptReferenceEntity)
                {
                    entityFieldName.assign(propertyName);
                    const int otherIndex = find_script_reference_property_index(
                        a_marionnetteClass,
                        baseName,
                        MarionnettePropertyReferenceRole::ScriptReferenceClass);
                    if (otherIndex < 0)
                    {
                        return false;
                    }

                    classPropertyIndex = static_cast<uint32_t>(otherIndex);
                    classFieldName.assign(
                        a_marionnetteClass.properties[classPropertyIndex].name);
                    if (classPropertyIndex < entityPropertyIndex)
                    {
                        return false;
                    }
                }
                else if (property.referenceRole ==
                    MarionnettePropertyReferenceRole::ScriptReferenceClass)
                {
                    classFieldName.assign(propertyName);
                    const int otherIndex = find_script_reference_property_index(
                        a_marionnetteClass,
                        baseName,
                        MarionnettePropertyReferenceRole::ScriptReferenceEntity);
                    if (otherIndex < 0)
                    {
                        return false;
                    }

                    entityPropertyIndex = static_cast<uint32_t>(otherIndex);
                    entityFieldName.assign(
                        a_marionnetteClass.properties[entityPropertyIndex].name);
                    if (entityPropertyIndex < classPropertyIndex)
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }

            const MarionnetteProperty& entityProperty =
                a_marionnetteClass.properties[entityPropertyIndex];
            const MarionnetteProperty& classProperty =
                a_marionnetteClass.properties[classPropertyIndex];
            if (entityProperty.type != MarionnettePropertyType::EntityRef ||
                classProperty.type != MarionnettePropertyType::ClassRef)
            {
                return false;
            }

            const int entityFieldIndex = find_script_field_index(
                a_fieldValues,
                entityFieldName);
            const int classFieldIndex = find_script_field_index(
                a_fieldValues,
                classFieldName);
            if (entityFieldIndex < 0 || classFieldIndex < 0)
            {
                return false;
            }

            a_outPair.baseName = std::move(baseName);
            a_outPair.entityProperty = &entityProperty;
            a_outPair.classProperty = &classProperty;
            a_outPair.entityFieldValue =
                &a_fieldValues[static_cast<size_t>(entityFieldIndex)];
            a_outPair.classFieldValue =
                &a_fieldValues[static_cast<size_t>(classFieldIndex)];
            a_outPair.entityPropertyIndex = entityPropertyIndex;
            a_outPair.classPropertyIndex = classPropertyIndex;
            return true;
        }

        [[nodiscard]] static int find_script_reference_property_index(
            const MarionnetteClass& a_marionnetteClass,
            std::string_view a_groupName,
            MarionnettePropertyReferenceRole a_referenceRole) noexcept
        {
            for (uint32_t propertyIndex = 0;
                propertyIndex < a_marionnetteClass.propertyCount;
                ++propertyIndex)
            {
                const MarionnetteProperty& property =
                    a_marionnetteClass.properties[propertyIndex];
                if (property.groupName == nullptr)
                {
                    continue;
                }

                if (std::string_view(property.groupName) == a_groupName &&
                    property.referenceRole == a_referenceRole)
                {
                    return static_cast<int>(propertyIndex);
                }
            }

            return -1;
        }

        static void assign_script_field_values_to_component(
            ECS::ScriptComponent& a_component,
            const MarionnetteClass& a_marionnetteClass,
            const std::vector<ECS::ScriptFieldValue>& a_fieldValues)
        {
            a_component.serializedFieldValues.clear();
            a_component.transientFieldValues.clear();
            for (uint32_t propertyIndex = 0;
                propertyIndex < a_marionnetteClass.propertyCount;
                ++propertyIndex)
            {
                const MarionnetteProperty& property =
                    a_marionnetteClass.properties[propertyIndex];
                if (property.name == nullptr)
                {
                    continue;
                }

                const int fieldIndex =
                    find_script_field_index(a_fieldValues, property.name);
                if (fieldIndex < 0)
                {
                    continue;
                }

                set_script_field_value(
                    a_component,
                    get_script_field_storage(property),
                    a_fieldValues[static_cast<size_t>(fieldIndex)]);
            }
        }

        [[nodiscard]] static std::vector<ECS::ScriptFieldValue>
            build_resolved_script_field_values(
                const ECS::ScriptComponent& a_component,
                const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues,
                const MarionnetteClass* a_marionnetteClass)
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
                ScriptFieldStorage preferredStorage =
                    ScriptFieldStorage::Serialized;
                if (a_marionnetteClass != nullptr)
                {
                    const MarionnetteProperty* property =
                        a_marionnetteClass->find_property(resolvedField.name);
                    if (property != nullptr)
                    {
                        preferredStorage = get_script_field_storage(*property);
                    }
                }

                const ECS::ScriptFieldValue* currentField = find_script_field_value(
                    a_component,
                    resolvedField.name,
                    preferredStorage);
                if (currentField == nullptr)
                {
                    continue;
                }

                if (currentField->type != resolvedField.type)
                {
                    continue;
                }

                resolvedField = *currentField;
            }

            return resolvedFieldValues;
        }

        [[nodiscard]] ScriptFieldDiagnostics diagnose_script_fields(
            const ECS::ScriptComponent& a_component,
            const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues,
            const MarionnetteClass* a_marionnetteClass) const noexcept
        {
            ScriptFieldDiagnostics diagnostics{};

            for (const ECS::ScriptFieldValue& defaultField : a_defaultFieldValues)
            {
                ScriptFieldStorage preferredStorage =
                    ScriptFieldStorage::Serialized;
                if (a_marionnetteClass != nullptr)
                {
                    const MarionnetteProperty* property =
                        a_marionnetteClass->find_property(defaultField.name);
                    if (property != nullptr)
                    {
                        preferredStorage = get_script_field_storage(*property);
                    }
                }

                ScriptFieldStorage actualStorage = preferredStorage;
                const ECS::ScriptFieldValue* currentField = find_script_field_value(
                    a_component,
                    defaultField.name,
                    preferredStorage,
                    &actualStorage);
                if (currentField == nullptr)
                {
                    ++diagnostics.missingFieldCount;
                    continue;
                }

                if (currentField->type != defaultField.type)
                {
                    ++diagnostics.typeMismatchCount;
                }
                else if (actualStorage != preferredStorage)
                {
                    ++diagnostics.storageMismatchCount;
                }
                else if (defaultField.type == ECS::ScriptFieldType::ClassRef &&
                    !currentField->classValue.empty() &&
                    !is_registered_script_class_name(currentField->classValue))
                {
                    ++diagnostics.unresolvedClassReferenceCount;
                }
            }

            const std::vector<ECS::ScriptFieldValue> currentFields =
                collect_script_field_values(a_component);
            for (const ECS::ScriptFieldValue& currentField : currentFields)
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

        [[nodiscard]] static const char* to_string(
            ECS::ScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case ECS::ScriptFieldType::Float:
                return "Float";
            case ECS::ScriptFieldType::Int32:
                return "Int32";
            case ECS::ScriptFieldType::Bool:
                return "Bool";
            case ECS::ScriptFieldType::EntityRef:
                return "EntityRef";
            case ECS::ScriptFieldType::ClassRef:
                return "ClassRef";
            }

            return "Unknown";
        }

        [[nodiscard]] static const char* to_string(
            MarionnettePropertyType a_type) noexcept
        {
            switch (a_type)
            {
            case MarionnettePropertyType::Float:
                return "Float";
            case MarionnettePropertyType::Int32:
                return "Int32";
            case MarionnettePropertyType::Bool:
                return "Bool";
            case MarionnettePropertyType::EntityRef:
                return "EntityRef";
            case MarionnettePropertyType::ClassRef:
                return "ClassRef";
            }

            return "Unknown";
        }

        [[nodiscard]] static const char* to_string(
            ScriptFieldStorage a_storage) noexcept
        {
            switch (a_storage)
            {
            case ScriptFieldStorage::Serialized:
                return "Serialized";
            case ScriptFieldStorage::Transient:
                return "Transient";
            }

            return "Unknown";
        }

        [[nodiscard]] static bool field_types_match(
            ECS::ScriptFieldType a_fieldType,
            MarionnettePropertyType a_propertyType) noexcept
        {
            switch (a_fieldType)
            {
            case ECS::ScriptFieldType::Float:
                return a_propertyType == MarionnettePropertyType::Float;
            case ECS::ScriptFieldType::Int32:
                return a_propertyType == MarionnettePropertyType::Int32;
            case ECS::ScriptFieldType::Bool:
                return a_propertyType == MarionnettePropertyType::Bool;
            case ECS::ScriptFieldType::EntityRef:
                return a_propertyType == MarionnettePropertyType::EntityRef;
            case ECS::ScriptFieldType::ClassRef:
                return a_propertyType == MarionnettePropertyType::ClassRef;
            }

            return false;
        }

        [[nodiscard]] std::vector<ScriptSchemaFieldStatus>
            build_script_schema_field_statuses(
                const ECS::ScriptComponent& a_component,
                const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues,
                const MarionnetteClass& a_marionnetteClass) const
        {
            std::vector<ScriptSchemaFieldStatus> statuses{};
            statuses.reserve(
                static_cast<size_t>(a_marionnetteClass.propertyCount) +
                a_component.serializedFieldValues.size() +
                a_component.transientFieldValues.size());

            for (uint32_t propertyIndex = 0;
                propertyIndex < a_marionnetteClass.propertyCount;
                ++propertyIndex)
            {
                const MarionnetteProperty& property =
                    a_marionnetteClass.properties[propertyIndex];
                if (property.name == nullptr)
                {
                    continue;
                }

                ScriptSchemaFieldStatus status{};
                status.fieldName = property.name;
                status.expectedTypeName = to_string(property.type);
                const ScriptFieldStorage preferredStorage =
                    get_script_field_storage(property);
                status.expectedStorageName = to_string(preferredStorage);

                ScriptFieldStorage actualStorage = preferredStorage;
                const ECS::ScriptFieldValue* currentField = find_script_field_value(
                    a_component,
                    status.fieldName,
                    preferredStorage,
                    &actualStorage);
                if (currentField == nullptr)
                {
                    status.kind = ScriptSchemaFieldStatus::Kind::Missing;
                    statuses.push_back(std::move(status));
                    continue;
                }

                status.currentTypeName = to_string(currentField->type);
                status.currentStorageName = to_string(actualStorage);
                if (!field_types_match(currentField->type, property.type))
                {
                    status.kind = ScriptSchemaFieldStatus::Kind::TypeMismatch;
                }
                else if (actualStorage != preferredStorage)
                {
                    status.kind = ScriptSchemaFieldStatus::Kind::StorageMismatch;
                }
                else if (currentField->type == ECS::ScriptFieldType::ClassRef &&
                    !currentField->classValue.empty() &&
                    !is_registered_script_class_name(currentField->classValue))
                {
                    status.kind =
                        ScriptSchemaFieldStatus::Kind::UnresolvedClassReference;
                    status.referencedClassName = currentField->classValue;
                }
                else
                {
                    status.kind = ScriptSchemaFieldStatus::Kind::Ok;
                }
                statuses.push_back(std::move(status));
            }

            const std::vector<ECS::ScriptFieldValue> currentFields =
                collect_script_field_values(a_component);
            for (const ECS::ScriptFieldValue& currentField : currentFields)
            {
                const MarionnetteProperty* property =
                    a_marionnetteClass.find_property(currentField.name);
                if (property != nullptr)
                {
                    continue;
                }

                ScriptSchemaFieldStatus status{};
                status.fieldName = currentField.name;
                status.currentTypeName = to_string(currentField.type);
                status.kind = ScriptSchemaFieldStatus::Kind::Orphan;
                statuses.push_back(std::move(status));
            }

            if (statuses.empty() && !a_defaultFieldValues.empty())
            {
                statuses.reserve(a_defaultFieldValues.size());
                for (const ECS::ScriptFieldValue& fieldValue : a_defaultFieldValues)
                {
                    ScriptSchemaFieldStatus status{};
                    status.fieldName = fieldValue.name;
                    status.currentTypeName = to_string(fieldValue.type);
                    status.expectedTypeName = to_string(fieldValue.type);
                    status.currentStorageName =
                        to_string(ScriptFieldStorage::Serialized);
                    status.expectedStorageName =
                        to_string(ScriptFieldStorage::Serialized);
                    status.kind = ScriptSchemaFieldStatus::Kind::Ok;
                    statuses.push_back(std::move(status));
                }
            }

            return statuses;
        }

        void draw_script_schema_statuses(
            const ECS::ScriptComponent& a_component,
            const MarionnetteClass& a_marionnetteClass,
            const std::vector<ECS::ScriptFieldValue>& a_defaultFieldValues,
            const std::vector<ScriptSchemaFieldStatus>& a_statuses)
        {
            if (a_statuses.empty())
            {
                return;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("schema status");

            for (size_t statusIndex = 0; statusIndex < a_statuses.size(); ++statusIndex)
            {
                const ScriptSchemaFieldStatus& status = a_statuses[statusIndex];
                ImVec4 color = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
                const char* label = "OK";
                switch (status.kind)
                {
                case ScriptSchemaFieldStatus::Kind::Ok:
                    break;
                case ScriptSchemaFieldStatus::Kind::Missing:
                    color = ImVec4(1.0f, 0.80f, 0.35f, 1.0f);
                    label = "Missing";
                    break;
                case ScriptSchemaFieldStatus::Kind::TypeMismatch:
                    color = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
                    label = "TypeMismatch";
                    break;
                case ScriptSchemaFieldStatus::Kind::StorageMismatch:
                    color = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
                    label = "StorageMismatch";
                    break;
                case ScriptSchemaFieldStatus::Kind::UnresolvedClassReference:
                    color = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
                    label = "UnresolvedClassRef";
                    break;
                case ScriptSchemaFieldStatus::Kind::Orphan:
                    color = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
                    label = "Orphan";
                    break;
                }

                ImGui::PushID(static_cast<int>(statusIndex));
                ImGui::TextColored(color, "[%s] %s", label, status.fieldName.c_str());
                if (!status.expectedTypeName.empty() &&
                    !status.currentTypeName.empty() &&
                    status.expectedTypeName != status.currentTypeName)
                {
                    ImGui::Text("expected: %s / current: %s",
                        status.expectedTypeName.c_str(),
                        status.currentTypeName.c_str());
                }
                else if (!status.expectedTypeName.empty())
                {
                    ImGui::Text("type: %s", status.expectedTypeName.c_str());
                }
                else if (!status.currentTypeName.empty())
                {
                    ImGui::Text("saved type: %s", status.currentTypeName.c_str());
                }
                if (!status.expectedStorageName.empty() &&
                    !status.currentStorageName.empty() &&
                    status.expectedStorageName != status.currentStorageName)
                {
                    ImGui::Text("expected storage: %s / current: %s",
                        status.expectedStorageName.c_str(),
                        status.currentStorageName.c_str());
                }
                else if (!status.expectedStorageName.empty())
                {
                    ImGui::Text("storage: %s", status.expectedStorageName.c_str());
                }
                if (!status.referencedClassName.empty())
                {
                    ImGui::Text("class ref: %s",
                        status.referencedClassName.c_str());
                }

                switch (status.kind)
                {
                case ScriptSchemaFieldStatus::Kind::Ok:
                    break;

                case ScriptSchemaFieldStatus::Kind::Missing:
                {
                    if (ImGui::Button("追加"))
                    {
                        const int defaultFieldIndex = find_script_field_index(
                            a_defaultFieldValues, status.fieldName);
                        const MarionnetteProperty* property =
                            a_marionnetteClass.find_property(status.fieldName);
                        if (defaultFieldIndex >= 0 && property != nullptr)
                        {
                            ECS::ScriptComponent nextComponent = a_component;
                            set_script_field_value(
                                nextComponent,
                                get_script_field_storage(*property),
                                a_defaultFieldValues[static_cast<size_t>(defaultFieldIndex)]);
                            submit_set_script_component_command(nextComponent);
                        }
                    }
                    break;
                }

                case ScriptSchemaFieldStatus::Kind::TypeMismatch:
                {
                    if (ImGui::Button("既定値へ戻す"))
                    {
                        const int defaultFieldIndex = find_script_field_index(
                            a_defaultFieldValues, status.fieldName);
                        const MarionnetteProperty* property =
                            a_marionnetteClass.find_property(status.fieldName);
                        if (defaultFieldIndex >= 0 && property != nullptr)
                        {
                            ECS::ScriptComponent nextComponent = a_component;
                            set_script_field_value(
                                nextComponent,
                                get_script_field_storage(*property),
                                a_defaultFieldValues[static_cast<size_t>(defaultFieldIndex)]);
                            submit_set_script_component_command(nextComponent);
                        }
                    }
                    break;
                }

                case ScriptSchemaFieldStatus::Kind::StorageMismatch:
                {
                    if (ImGui::Button("正しい配列へ移動"))
                    {
                        const MarionnetteProperty* property =
                            a_marionnetteClass.find_property(status.fieldName);
                        if (property != nullptr)
                        {
                            ScriptFieldStorage currentStorage =
                                get_script_field_storage(*property);
                            const ECS::ScriptFieldValue* currentField =
                                find_script_field_value(
                                    a_component,
                                    status.fieldName,
                                    currentStorage,
                                    &currentStorage);
                            if (currentField != nullptr)
                            {
                                ECS::ScriptComponent nextComponent = a_component;
                                set_script_field_value(
                                    nextComponent,
                                    get_script_field_storage(*property),
                                    *currentField);
                                submit_set_script_component_command(nextComponent);
                            }
                        }
                    }
                    break;
                }

                case ScriptSchemaFieldStatus::Kind::UnresolvedClassReference:
                {
                    ImGui::TextWrapped(
                        "参照先 class が未登録です。GameScript を再ビルドするか、有効な class を選択してください。");
                    const MarionnetteProperty* property =
                        a_marionnetteClass.find_property(status.fieldName);
                    if (property != nullptr)
                    {
                        ScriptFieldStorage currentStorage =
                            get_script_field_storage(*property);
                        const ECS::ScriptFieldValue* currentField =
                            find_script_field_value(
                                a_component,
                                status.fieldName,
                                currentStorage,
                                &currentStorage);
                        if (currentField != nullptr)
                        {
                            if (ImGui::Button("クリア"))
                            {
                                ECS::ScriptComponent nextComponent = a_component;
                                ECS::ScriptFieldValue nextFieldValue = *currentField;
                                nextFieldValue.classValue.clear();
                                set_script_field_value(
                                    nextComponent,
                                    get_script_field_storage(*property),
                                    nextFieldValue);
                                submit_set_script_component_command(nextComponent);
                            }

                            const int defaultFieldIndex = find_script_field_index(
                                a_defaultFieldValues,
                                status.fieldName);
                            if (defaultFieldIndex >= 0)
                            {
                                ImGui::SameLine();
                                if (ImGui::Button("既定値へ戻す"))
                                {
                                    ECS::ScriptComponent nextComponent =
                                        a_component;
                                    set_script_field_value(
                                        nextComponent,
                                        get_script_field_storage(*property),
                                        a_defaultFieldValues[static_cast<size_t>(
                                            defaultFieldIndex)]);
                                    submit_set_script_component_command(
                                        nextComponent);
                                }
                            }

                            if (!a_component.className.empty() &&
                                is_registered_script_class_name(
                                    a_component.className))
                            {
                                ImGui::SameLine();
                                if (ImGui::Button("現在の Script を設定"))
                                {
                                    ECS::ScriptComponent nextComponent =
                                        a_component;
                                    ECS::ScriptFieldValue nextFieldValue =
                                        *currentField;
                                    nextFieldValue.classValue =
                                        a_component.className;
                                    set_script_field_value(
                                        nextComponent,
                                        get_script_field_storage(*property),
                                        nextFieldValue);
                                    submit_set_script_component_command(
                                        nextComponent);
                                }
                            }
                        }
                    }
                    break;
                }

                case ScriptSchemaFieldStatus::Kind::Orphan:
                {
                    if (ImGui::Button("削除"))
                    {
                        if (find_script_field_value(
                                a_component,
                                status.fieldName,
                                ScriptFieldStorage::Serialized) != nullptr)
                        {
                            ECS::ScriptComponent nextComponent = a_component;
                            erase_script_field_value(
                                nextComponent.serializedFieldValues,
                                status.fieldName);
                            erase_script_field_value(
                                nextComponent.transientFieldValues,
                                status.fieldName);
                            submit_set_script_component_command(nextComponent);
                        }
                    }
                    break;
                }
                }

                ImGui::PopID();
            }
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

        void submit_set_script_component_command(
            GameCore::EntityId a_entityId,
            const ECS::ScriptComponent& a_component)
        {
            if (editorBridge == nullptr ||
                a_entityId == GameCore::k_invalidEntityId)
            {
                return;
            }

            Result result = editorBridge->submit_command(
                std::make_unique<SetScriptComponentCommand>(
                    a_entityId, a_component));
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

        [[nodiscard]] bool is_registered_script_class_name(
            std::string_view a_className) const noexcept
        {
            if (a_className.empty())
            {
                return true;
            }

            return m_engine != nullptr &&
                m_engine->has_registered_script_class(a_className);
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
