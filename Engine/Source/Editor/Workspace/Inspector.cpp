#include "Inspector.h"

// === Runtime includes ===
#include <CQRS/CQRS.h>
#include <Command/Commands.h>
#include <DrawSystem/MeshPool.h>
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace
{
    int render_queue_index(Cue::ECS::RenderQueue a_queue) noexcept
    {
        switch (a_queue)
        {
        case Cue::ECS::RenderQueue::Opaque:
            return 0;
        case Cue::ECS::RenderQueue::Transparent:
            return 1;
        case Cue::ECS::RenderQueue::Auto:
        default:
            return 2;
        }
    }

    Cue::ECS::RenderQueue render_queue_from_index(int a_index) noexcept
    {
        switch (a_index)
        {
        case 0:
            return Cue::ECS::RenderQueue::Opaque;
        case 1:
            return Cue::ECS::RenderQueue::Transparent;
        case 2:
        default:
            return Cue::ECS::RenderQueue::Auto;
        }
    }

    int shadow_caster_mode_index(Cue::ECS::ShadowCasterMode a_mode) noexcept
    {
        switch (a_mode)
        {
        case Cue::ECS::ShadowCasterMode::TwoSided:
            return 1;
        case Cue::ECS::ShadowCasterMode::Solid:
        default:
            return 0;
        }
    }

    Cue::ECS::ShadowCasterMode shadow_caster_mode_from_index(int a_index) noexcept
    {
        switch (a_index)
        {
        case 1:
            return Cue::ECS::ShadowCasterMode::TwoSided;
        case 0:
        default:
            return Cue::ECS::ShadowCasterMode::Solid;
        }
    }

    void text_entity_id(const char* a_label, Cue::GameCore::EntityId a_entityId)
    {
        const std::string text =
            a_entityId == Cue::GameCore::k_invalidEntityId
                ? "invalid"
                : std::to_string(static_cast<unsigned long long>(a_entityId));
        ImGui::Text("%s: %s", a_label, text.c_str());
    }

    void text_scene_id(const char* a_label, Cue::GameCore::SceneId a_sceneId)
    {
        const std::string text =
            a_sceneId == Cue::GameCore::k_invalidSceneId
                ? "invalid"
                : std::to_string(static_cast<unsigned long long>(a_sceneId));
        ImGui::Text("%s: %s", a_label, text.c_str());
    }

    void text_float3(const char* a_label, const Cue::Math::float3& a_value)
    {
        ImGui::Text("%s: %.3f, %.3f, %.3f", a_label, a_value.x, a_value.y, a_value.z);
    }

    [[nodiscard]] Cue::Math::float3
    radians_to_degrees(const Cue::Math::float3& a_radians) noexcept
    {
        constexpr float radToDeg = 180.0f / std::numbers::pi_v<float>;
        return Cue::Math::float3(a_radians.x * radToDeg, a_radians.y * radToDeg,
                                 a_radians.z * radToDeg);
    }

    [[nodiscard]] Cue::Math::float3
    degrees_to_radians(const Cue::Math::float3& a_degrees) noexcept
    {
        constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
        return Cue::Math::float3(a_degrees.x * degToRad, a_degrees.y * degToRad,
                                 a_degrees.z * degToRad);
    }

    [[nodiscard]] bool
    equivalent_rotation(const Cue::Math::Quaternion& a_left,
                        const Cue::Math::Quaternion& a_right) noexcept
    {
        constexpr float epsilon = 0.0001f;
        const Cue::Math::Quaternion left = Cue::Math::Quaternion::normalize(a_left);
        const Cue::Math::Quaternion right = Cue::Math::Quaternion::normalize(a_right);
        const Cue::Math::Quaternion negatedRight(-right.x, -right.y, -right.z,
                                                 -right.w);
        return Cue::Math::Quaternion::equals_epsilon(left, right, epsilon) ||
               Cue::Math::Quaternion::equals_epsilon(left, negatedRight, epsilon);
    }

    [[nodiscard]] Cue::Math::float3 euler_degrees_from_quaternion(
        const Cue::Math::Quaternion& a_rotation) noexcept
    {
        return radians_to_degrees(Cue::Math::quaternion_to_euler_xyz(a_rotation));
    }

    [[nodiscard]] Cue::Math::Quaternion quaternion_from_euler_degrees(
        const Cue::Math::float3& a_eulerDegrees) noexcept
    {
        return Cue::Math::quaternion_from_euler_xyz(
            degrees_to_radians(a_eulerDegrees));
    }

    void text_euler_degrees(const char* a_label,
                            const Cue::Math::Quaternion& a_value)
    {
        const Cue::Math::float3 euler = euler_degrees_from_quaternion(a_value);
        ImGui::Text("%s: %.3f, %.3f, %.3f deg", a_label, euler.x, euler.y, euler.z);
    }

    template <typename Component>
    [[nodiscard]] bool
    object_has_component(Cue::GameCore::GameObject& a_object) noexcept
    {
        bool hasComponent = false;
        return a_object.has_component<Component>(hasComponent) && hasComponent;
    }

    template <size_t Size>
    void copy_text_to_buffer(std::array<char, Size>& a_buffer, std::string_view a_text)
    {
        std::fill(a_buffer.begin(), a_buffer.end(), '\0');
        const size_t copyLength = (std::min)(a_text.size(), a_buffer.size() - 1u);
        if (copyLength > 0)
        {
            std::memcpy(a_buffer.data(), a_text.data(), copyLength);
        }
    }
} // namespace

namespace Cue::Editor
{
    Inspector::Inspector(Core::CQRS::Bridge* a_commandBridge,
                         GameCore::GameWorld* a_gameWorld,
                         DrawSystem::MeshPool* a_meshPool,
                         GameCore::EntityId* a_selectedEntityId,
                         AssetSelection* a_selectedAsset) noexcept
        : m_gameWorld(a_gameWorld), m_meshPool(a_meshPool),
          m_selectedEntityId(a_selectedEntityId), m_selectedAsset(a_selectedAsset),
          m_commandBridge(a_commandBridge)
    {
    }

    void Inspector::set_game_world(GameCore::GameWorld* a_gameWorld) noexcept
    {
        m_gameWorld = a_gameWorld;
    }

    void Inspector::set_mesh_pool(DrawSystem::MeshPool* a_meshPool) noexcept
    {
        m_meshPool = a_meshPool;
    }

    void Inspector::update()
    {
        ImGui::Begin("インスペクター");

        if (m_selectedAsset != nullptr && !m_selectedAsset->path.is_empty())
        {
            draw_asset_selection();
            ImGui::End();
            return;
        }

        if (m_gameWorld == nullptr || m_selectedEntityId == nullptr)
        {
            ImGui::TextUnformatted("Inspector の依存が初期化されていません。");
            ImGui::End();
            return;
        }

        if (*m_selectedEntityId == GameCore::k_invalidEntityId)
        {
            ImGui::TextUnformatted(
                "ヒエラルキーまたは Asset Browser で対象を選択してください。");
            ImGui::End();
            return;
        }

        GameCore::GameObject object{};
        if (!find_selected_object(object))
        {
            // Hierarchy 側の削除や Scene 切替後に残った選択は Editor
            // 共有状態からも破棄する
            *m_selectedEntityId = GameCore::k_invalidEntityId;
            ImGui::TextUnformatted("選択中の GameObject は存在しません。");
            ImGui::End();
            return;
        }

        std::string objectName{};
        Result nameResult = object.name(objectName);
        if (!nameResult || objectName.empty())
        {
            objectName = "GameObject";
        }

        ImGui::Text("Name: %s", objectName.c_str());
        text_entity_id("EntityId", object.entity_id());
        ImGui::Separator();

        draw_add_component_menu(object);
        ImGui::Separator();

        draw_base_component(object);
        draw_transform_component(object);
        draw_world_transform_component(object);
        draw_camera_component(object);
        draw_mesh_filter_component(object);
        draw_static_mesh_renderer_component(object);
        draw_renderable_info_component(object);

        ImGui::End();
    }

    void Inspector::draw_asset_selection()
    {
        const AssetSelection& selectedAsset = *m_selectedAsset;
        const std::string extension = selectedAsset.path.extension();

        ImGui::Text("Name: %s", selectedAsset.path.filename().c_str());
        ImGui::Text("Type: %s", asset_kind_name(selectedAsset.kind));
        ImGui::Text("Extension: %s", extension.empty() ? "None" : extension.c_str());
        ImGui::Text("Size: %llu bytes",
                    static_cast<unsigned long long>(selectedAsset.sizeBytes));
        ImGui::Separator();
        ImGui::TextUnformatted("Path:");
        ImGui::TextWrapped("%s", selectedAsset.path.utf8().c_str());
    }

    bool Inspector::find_selected_object(GameCore::GameObject& a_outObject)
    {
        bool found = false;
        const GameCore::EntityId selectedEntityId = *m_selectedEntityId;
        const Result result = m_gameWorld->for_each_object(
            [&a_outObject, &found, selectedEntityId](GameCore::EntityId a_entityId,
                                                     GameCore::GameObject a_object) {
                if (found || a_entityId != selectedEntityId)
                {
                    return;
                }

                a_outObject = a_object;
                found = a_object.is_valid();
            });

        return result && found;
    }

    void Inspector::draw_add_component_menu(GameCore::GameObject& a_object)
    {
        const bool hasTransform =
            object_has_component<ECS::TransformComponent>(a_object);
        const bool hasCamera = object_has_component<ECS::CameraComponent>(a_object);
        const bool hasMeshFilter =
            object_has_component<ECS::MeshFilterComponent>(a_object);
        const bool hasStaticMeshRenderer =
            object_has_component<ECS::StaticMeshRendererComponent>(a_object);
        const bool hasAddableComponent =
            !hasTransform || !hasCamera || !hasMeshFilter || !hasStaticMeshRenderer;

        if (ImGui::Button("Add Component"))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            if (!hasTransform && ImGui::MenuItem("Transform"))
            {
                submit_add_component(a_object.entity_id(), ComponentKind::Transform);
            }
            if (!hasCamera && ImGui::MenuItem("Camera"))
            {
                submit_add_component(a_object.entity_id(), ComponentKind::Camera);
            }
            if (!hasMeshFilter && ImGui::MenuItem("Mesh Filter"))
            {
                submit_add_component(a_object.entity_id(), ComponentKind::MeshFilter);
            }
            if (!hasStaticMeshRenderer && ImGui::MenuItem("Static Mesh Renderer"))
            {
                submit_add_component(a_object.entity_id(),
                                     ComponentKind::StaticMeshRenderer);
            }
            if (!hasAddableComponent)
            {
                ImGui::MenuItem("追加可能な Component はありません", nullptr, false,
                                false);
            }

            ImGui::EndPopup();
        }
    }

    void Inspector::draw_base_component(GameCore::GameObject& a_object)
    {
        GameCore::BaseComponent* base = nullptr;
        if (!a_object.get_component(base) || base == nullptr)
        {
            return;
        }

        if (m_baseEntityId != a_object.entity_id())
        {
            m_baseEntityId = a_object.entity_id();
            m_isNameEditing = false;
            m_isTagEditing = false;
            copy_text_to_buffer(m_nameBuffer, base->name);
            copy_text_to_buffer(m_tagBuffer, base->tag);
        }

        if (ImGui::CollapsingHeader("Base", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (!m_isNameEditing)
            {
                copy_text_to_buffer(m_nameBuffer, base->name);
            }
            const bool isNameSubmitted = ImGui::InputText(
                "Name", m_nameBuffer.data(), m_nameBuffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool isNameDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
            m_isNameEditing = ImGui::IsItemActive();
            if (isNameSubmitted || isNameDeactivatedAfterEdit)
            {
                const std::string name = m_nameBuffer.data();
                if (name != base->name)
                {
                    submit_rename_object(a_object.entity_id(), name);
                }
            }

            if (!m_isTagEditing)
            {
                copy_text_to_buffer(m_tagBuffer, base->tag);
            }
            const bool isTagSubmitted = ImGui::InputText(
                "Tag", m_tagBuffer.data(), m_tagBuffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool isTagDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
            m_isTagEditing = ImGui::IsItemActive();
            if (isTagSubmitted || isTagDeactivatedAfterEdit)
            {
                const std::string tag = m_tagBuffer.data();
                if (tag != base->tag)
                {
                    submit_object_tag(a_object.entity_id(), tag);
                }
            }

            text_scene_id("OwningScene", base->owningSceneId);
            text_entity_id("Parent", base->parent);

            bool isActive = base->isActiveSelf;
            if (ImGui::Checkbox("ActiveSelf", &isActive))
            {
                submit_object_active(a_object.entity_id(), isActive);
            }

            bool isPersistent = base->isPersistent;
            if (ImGui::Checkbox("Persistent", &isPersistent))
            {
                submit_object_persistent(a_object.entity_id(), isPersistent);
            }
        }
    }

    void Inspector::draw_transform_component(GameCore::GameObject& a_object)
    {
        ECS::TransformComponent* transform = nullptr;
        if (!a_object.get_component(transform) || transform == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ECS::TransformComponent edited = *transform;
            sync_rotation_cache(a_object.entity_id(), edited);

            float position[3] = {edited.position.x, edited.position.y,
                                 edited.position.z};
            const bool hasPositionChanged = ImGui::DragFloat3("Position", position, 0.05f);
            const uint64_t positionTransactionId = current_history_transaction();
            if (hasPositionChanged)
            {
                edited.position = Math::float3(position[0], position[1], position[2]);
                submit_transform_component(a_object.entity_id(), edited, positionTransactionId);
            }

            float rotation[3] = {m_rotationEulerDegrees.x, m_rotationEulerDegrees.y,
                                 m_rotationEulerDegrees.z};
            const bool hasRotationChanged =
                ImGui::DragFloat3("Rotation", rotation, 0.1f, 0.0f, 0.0f, "%.3f deg");
            const uint64_t rotationTransactionId = current_history_transaction();
            if (hasRotationChanged)
            {
                m_rotationEulerDegrees =
                    Math::float3(rotation[0], rotation[1], rotation[2]);
                edited.rotation = quaternion_from_euler_degrees(m_rotationEulerDegrees);
                m_rotationSource = edited.rotation;
                submit_transform_component(a_object.entity_id(), edited, rotationTransactionId);
            }
            m_isRotationEditing = ImGui::IsItemActive();

            float scale[3] = {edited.scale.x, edited.scale.y, edited.scale.z};
            const bool hasScaleChanged = ImGui::DragFloat3("Scale", scale, 0.05f);
            const uint64_t scaleTransactionId = current_history_transaction();
            if (hasScaleChanged)
            {
                edited.scale = Math::float3(scale[0], scale[1], scale[2]);
                submit_transform_component(a_object.entity_id(), edited, scaleTransactionId);
            }
        }
    }

    void Inspector::draw_world_transform_component(GameCore::GameObject& a_object)
    {
        ECS::WorldTransformComponent* worldTransform = nullptr;
        if (!a_object.get_component(worldTransform) || worldTransform == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("World Transform"))
        {
            text_float3("Position", worldTransform->position);
            text_euler_degrees("Rotation", worldTransform->rotation);
            text_float3("Scale", worldTransform->scale);
        }
    }

    void Inspector::draw_camera_component(GameCore::GameObject& a_object)
    {
        ECS::CameraComponent* camera = nullptr;
        if (!a_object.get_component(camera) || camera == nullptr)
        {
            return;
        }

        const bool isOpen = ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen);
        draw_remove_component_button(a_object.entity_id(), ComponentKind::Camera, "Camera");
        if (isOpen)
        {
            ECS::CameraComponent edited = *camera;

            const bool hasFovYChanged = ImGui::DragFloat("FovY", &edited.fovY, 0.1f, 1.0f,
                                                          179.0f, "%.3f deg");
            const uint64_t fovYTransactionId = current_history_transaction();
            if (hasFovYChanged)
            {
                edited.fovY = std::clamp(edited.fovY, 1.0f, 179.0f);
                submit_camera_component(a_object.entity_id(), edited, fovYTransactionId);
            }
            const bool hasAspectRatioChanged =
                ImGui::DragFloat("AspectRatio", &edited.aspectRatio, 0.01f, 0.0f, 8.0f);
            const uint64_t aspectRatioTransactionId = current_history_transaction();
            if (hasAspectRatioChanged)
            {
                edited.aspectRatio = std::max(0.0f, edited.aspectRatio);
                submit_camera_component(a_object.entity_id(), edited,
                                        aspectRatioTransactionId);
            }
            const bool hasNearZChanged =
                ImGui::DragFloat("NearZ", &edited.nearZ, 0.01f, 0.001f, edited.farZ);
            const uint64_t nearZTransactionId = current_history_transaction();
            if (hasNearZChanged)
            {
                // projection 行列の破綻を避けるため near/far の最小間隔を維持する
                edited.nearZ = std::max(0.001f, edited.nearZ);
                edited.farZ = std::max(edited.nearZ + 0.001f, edited.farZ);
                submit_camera_component(a_object.entity_id(), edited, nearZTransactionId);
            }
            const bool hasFarZChanged = ImGui::DragFloat("FarZ", &edited.farZ, 0.1f,
                                                          edited.nearZ + 0.001f, 100000.0f);
            const uint64_t farZTransactionId = current_history_transaction();
            if (hasFarZChanged)
            {
                // UI 入力の順序に依存せず CameraComponent 側の不変条件を保つ
                edited.farZ = std::max(edited.nearZ + 0.001f, edited.farZ);
                submit_camera_component(a_object.entity_id(), edited, farZTransactionId);
            }
        }
    }

    void Inspector::draw_mesh_filter_component(GameCore::GameObject& a_object)
    {
        ECS::MeshFilterComponent* meshFilter = nullptr;
        if (!a_object.get_component(meshFilter) || meshFilter == nullptr)
        {
            return;
        }

        const bool isOpen = ImGui::CollapsingHeader("Mesh Filter", ImGuiTreeNodeFlags_DefaultOpen);
        draw_remove_component_button(a_object.entity_id(), ComponentKind::MeshFilter,
                                     "Mesh Filter");
        if (isOpen)
        {
            ImGui::Text("MeshId: %u", meshFilter->meshId);
            if (m_meshPool == nullptr)
            {
                ImGui::TextUnformatted("MeshPool が初期化されていません。");
                return;
            }

            std::vector<DrawSystem::MeshListItem> meshItems{};
            Result result = m_meshPool->collect_named_meshes(meshItems);
            if (!result)
            {
                ImGui::TextUnformatted("Mesh 一覧を取得できません。");
                return;
            }

            std::string currentName = meshFilter->meshId == ECS::k_invalidMeshId
                                          ? "None"
                                          : meshFilter->modelName;
            for (const DrawSystem::MeshListItem& item : meshItems)
            {
                if (item.meshId == meshFilter->meshId)
                {
                    currentName = item.name;
                    break;
                }
            }
            if (currentName.empty())
            {
                currentName = "Unknown";
            }

            if (ImGui::BeginCombo("Mesh", currentName.c_str()))
            {
                const bool isNoneSelected = meshFilter->meshId == ECS::k_invalidMeshId;
                if (ImGui::Selectable("None", isNoneSelected))
                {
                    ECS::MeshFilterComponent edited = *meshFilter;
                    edited.modelName.clear();
                    edited.meshId = ECS::k_invalidMeshId;
                    submit_mesh_filter_component(a_object.entity_id(), edited);
                }
                if (isNoneSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }

                for (const DrawSystem::MeshListItem& item : meshItems)
                {
                    const bool isSelected = item.meshId == meshFilter->meshId;
                    if (ImGui::Selectable(item.name.c_str(), isSelected))
                    {
                        ECS::MeshFilterComponent edited = *meshFilter;
                        edited.modelName = item.name;
                        edited.meshId = item.meshId;
                        submit_mesh_filter_component(a_object.entity_id(), edited);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
        }
    }

    void Inspector::draw_static_mesh_renderer_component(
        GameCore::GameObject& a_object)
    {
        ECS::StaticMeshRendererComponent* renderer = nullptr;
        if (!a_object.get_component(renderer) || renderer == nullptr)
        {
            return;
        }

        const bool isOpen = ImGui::CollapsingHeader("Static Mesh Renderer",
                                                    ImGuiTreeNodeFlags_DefaultOpen);
        draw_remove_component_button(a_object.entity_id(), ComponentKind::StaticMeshRenderer,
                                     "Static Mesh Renderer");
        if (isOpen)
        {
            ECS::StaticMeshRendererComponent edited = *renderer;

            const bool hasMaterialIdChanged =
                ImGui::InputScalar("MaterialId", ImGuiDataType_U32, &edited.materialId);
            const uint64_t materialIdTransactionId = current_history_transaction();
            if (hasMaterialIdChanged)
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited,
                                                      materialIdTransactionId);
            }

            const char* renderQueueItems[] = {"Opaque", "Transparent", "Auto"};
            int renderQueueIndex = render_queue_index(edited.renderQueue);
            if (ImGui::Combo("RenderQueue", &renderQueueIndex, renderQueueItems,
                             IM_ARRAYSIZE(renderQueueItems)))
            {
                edited.renderQueue = render_queue_from_index(renderQueueIndex);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            const char* shadowCasterItems[] = {"Solid", "TwoSided"};
            int shadowCasterIndex = shadow_caster_mode_index(edited.shadowCasterMode);
            if (ImGui::Combo("ShadowCaster", &shadowCasterIndex, shadowCasterItems,
                             IM_ARRAYSIZE(shadowCasterItems)))
            {
                edited.shadowCasterMode =
                    shadow_caster_mode_from_index(shadowCasterIndex);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            if (ImGui::Checkbox("Visible", &edited.visible))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }
            if (ImGui::Checkbox("CastsShadow", &edited.castsShadow))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }
            if (ImGui::Checkbox("ReceivesShadow", &edited.receivesShadow))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            const bool hasOverrideMaskChanged = ImGui::InputScalar(
                "OverrideMask", ImGuiDataType_U32, &edited.propertyBlock.overrideMask);
            const uint64_t overrideMaskTransactionId = current_history_transaction();
            if (hasOverrideMaskChanged)
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited,
                                                      overrideMaskTransactionId);
            }

            float color[4] = {
                edited.propertyBlock.color.x, edited.propertyBlock.color.y,
                edited.propertyBlock.color.z, edited.propertyBlock.color.w};
            const bool hasColorChanged = ImGui::ColorEdit4("Color", color);
            const uint64_t colorTransactionId = current_history_transaction();
            if (hasColorChanged)
            {
                edited.propertyBlock.color =
                    Math::float4(color[0], color[1], color[2], color[3]);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited,
                                                      colorTransactionId);
            }

            const bool hasShininessChanged = ImGui::DragFloat(
                "Shininess", &edited.propertyBlock.shininess, 0.1f, 0.0f, 4096.0f);
            const uint64_t shininessTransactionId = current_history_transaction();
            if (hasShininessChanged)
            {
                edited.propertyBlock.shininess =
                    std::max(0.0f, edited.propertyBlock.shininess);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited,
                                                      shininessTransactionId);
            }
            if (ImGui::Checkbox("UsesReflectionSkybox",
                                &edited.propertyBlock.usesReflectionSkybox))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }
        }
    }

    void Inspector::draw_renderable_info_component(GameCore::GameObject& a_object)
    {
        ECS::RenderableInfoComponent* renderableInfo = nullptr;
        if (!a_object.get_component(renderableInfo) || renderableInfo == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("Renderable Info"))
        {
            ImGui::Text("ObjectId: %u", renderableInfo->objectId);
            ImGui::Text("TransformId: %u", renderableInfo->transformId);
        }
    }

    void Inspector::sync_rotation_cache(
        GameCore::EntityId a_entityId,
        const ECS::TransformComponent& a_component) noexcept
    {
        const Math::Quaternion rotation =
            Math::Quaternion::normalize(a_component.rotation);
        if (!m_hasRotationCache || m_rotationEntityId != a_entityId)
        {
            m_rotationEntityId = a_entityId;
            m_rotationEulerDegrees = euler_degrees_from_quaternion(rotation);
            m_rotationSource = rotation;
            m_hasRotationCache = true;
            m_isRotationEditing = false;
            return;
        }

        if (m_isRotationEditing)
        {
            return;
        }

        if (!equivalent_rotation(rotation, m_rotationSource))
        {
            m_rotationEulerDegrees = euler_degrees_from_quaternion(rotation);
            m_rotationSource = rotation;
        }
    }

    void Inspector::submit_transform_component(
        GameCore::EntityId a_entityId, const ECS::TransformComponent& a_component,
        uint64_t a_historyTransactionId)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Component 変更は GameWorld の整合更新を通すため直接書き換えず command
        // 化する
        (void)m_commandBridge->submit_command(
            make_set_transform_component_command(a_entityId, a_component), a_historyTransactionId);
    }

    void Inspector::submit_camera_component(
        GameCore::EntityId a_entityId, const ECS::CameraComponent& a_component,
        uint64_t a_historyTransactionId)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Camera 更新後の描画 View 再計算を Engine 側の更新経路へ集約する
        (void)m_commandBridge->submit_command(
            make_set_camera_component_command(a_entityId, a_component), a_historyTransactionId);
    }

    void Inspector::submit_mesh_filter_component(
        GameCore::EntityId a_entityId,
        const ECS::MeshFilterComponent& a_component)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // MeshFilter の参照先変更は描画抽出の直前状態として Engine 側の更新順に揃える
        (void)m_commandBridge->submit_command(
            make_set_mesh_filter_component_command(a_entityId, a_component));
    }

    void Inspector::submit_static_mesh_renderer_component(
        GameCore::EntityId a_entityId,
        const ECS::StaticMeshRendererComponent& a_component,
        uint64_t a_historyTransactionId)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // RendererComponent は DrawSystem 抽出に関わるため CQRS
        // 経由で変更順序を揃える
        (void)m_commandBridge->submit_command(
            make_set_static_mesh_renderer_component_command(a_entityId, a_component),
            a_historyTransactionId);
    }

    uint64_t Inspector::current_history_transaction()
    {
        const uint32_t itemId = static_cast<uint32_t>(ImGui::GetItemID());
        if (ImGui::IsItemActivated())
        {
            m_activeHistoryItemId = itemId;
            m_activeHistoryTransactionId = m_nextHistoryTransactionId++;
        }

        const uint64_t transactionId = m_activeHistoryItemId == itemId
                                           ? m_activeHistoryTransactionId
                                           : 0;
        if (m_activeHistoryItemId == itemId && ImGui::IsItemDeactivatedAfterEdit())
        {
            // 次の入力で同じ ImGui ID が再利用されても、別操作として履歴を分離する。
            m_activeHistoryItemId = 0;
            m_activeHistoryTransactionId = 0;
        }

        return transactionId;
    }

    void Inspector::draw_remove_component_button(GameCore::EntityId a_entityId,
                                                 ComponentKind a_kind,
                                                 const char* a_componentName)
    {
        ImGui::SameLine();
        ImGui::PushID(a_componentName);
        if (ImGui::SmallButton("x"))
        {
            submit_remove_component(a_entityId, a_kind);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Remove %s Component", a_componentName);
        }
        ImGui::PopID();
    }

    void Inspector::submit_add_component(GameCore::EntityId a_entityId,
                                         ComponentKind a_kind)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Component 追加も Engine の更新境界へ集約し、ECS 走査中の構造変更を避ける
        (void)m_commandBridge->submit_command(
            std::make_unique<AddComponentCommand>(a_entityId, a_kind));
    }

    void Inspector::submit_remove_component(GameCore::EntityId a_entityId,
                                            ComponentKind a_kind)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Component の復元値を Command 側で保持し、Undo 時に削除前の設定を再現する。
        (void)m_commandBridge->submit_command(
            std::make_unique<RemoveComponentCommand>(a_entityId, a_kind));
    }

    void Inspector::submit_rename_object(GameCore::EntityId a_entityId, std::string a_name)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Name index の正規化を GameWorld へ集約するため、Inspector は新しい表示名だけを渡す。
        (void)m_commandBridge->submit_command(
            std::make_unique<RenameObjectCommand>(a_entityId, std::move(a_name)));
    }

    void Inspector::submit_object_tag(GameCore::EntityId a_entityId, std::string a_tag)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Tag index の更新と空 Tag の正規化を GameWorld 側へ残す。
        (void)m_commandBridge->submit_command(
            make_set_object_tag_command(a_entityId, std::move(a_tag)));
    }

    void Inspector::submit_object_active(GameCore::EntityId a_entityId, bool a_isActive)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // ECS の有効状態も同期する必要があるため BaseComponent を直接変更しない。
        (void)m_commandBridge->submit_command(
            make_set_object_active_command(a_entityId, a_isActive));
    }

    void Inspector::submit_object_persistent(
        GameCore::EntityId a_entityId,
        bool a_isPersistent)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Persistent 化に伴う Scene 所属の調整は GameWorld が一貫して扱う。
        (void)m_commandBridge->submit_command(
            make_set_object_persistent_command(a_entityId, a_isPersistent));
    }
} // namespace Cue::Editor
