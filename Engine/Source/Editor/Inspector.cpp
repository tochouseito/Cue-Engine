#include "Inspector.h"

// === Runtime includes ===
#include <Command/Commands.h>
#include <CQRS/CQRS.h>
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <string>

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
        const std::string text = a_entityId == Cue::GameCore::k_invalidEntityId
            ? "invalid"
            : std::to_string(static_cast<unsigned long long>(a_entityId));
        ImGui::Text("%s: %s", a_label, text.c_str());
    }

    void text_scene_id(const char* a_label, Cue::GameCore::SceneId a_sceneId)
    {
        const std::string text = a_sceneId == Cue::GameCore::k_invalidSceneId
            ? "invalid"
            : std::to_string(static_cast<unsigned long long>(a_sceneId));
        ImGui::Text("%s: %s", a_label, text.c_str());
    }

    void text_float3(const char* a_label, const Cue::Math::float3& a_value)
    {
        ImGui::Text("%s: %.3f, %.3f, %.3f",
            a_label, a_value.x, a_value.y, a_value.z);
    }

    void text_quaternion(const char* a_label, const Cue::Math::Quaternion& a_value)
    {
        ImGui::Text("%s: %.3f, %.3f, %.3f, %.3f",
            a_label, a_value.x, a_value.y, a_value.z, a_value.w);
    }
}

namespace Cue::Editor
{
    Inspector::Inspector(Core::CQRS::Bridge* a_commandBridge,
                         GameCore::GameWorld* a_gameWorld,
                         GameCore::EntityId* a_selectedEntityId) noexcept
        : m_gameWorld(a_gameWorld),
          m_selectedEntityId(a_selectedEntityId),
          m_commandBridge(a_commandBridge)
    {
    }

    void Inspector::set_game_world(GameCore::GameWorld* a_gameWorld) noexcept
    {
        m_gameWorld = a_gameWorld;
    }

    void Inspector::update()
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
            ImGui::TextUnformatted("ヒエラルキーで GameObject を選択してください。");
            ImGui::End();
            return;
        }

        GameCore::GameObject object{};
        if (!find_selected_object(object))
        {
            // Hierarchy 側の削除や Scene 切替後に残った選択は Editor 共有状態からも破棄する
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

        draw_base_component(object);
        draw_transform_component(object);
        draw_world_transform_component(object);
        draw_camera_component(object);
        draw_mesh_filter_component(object);
        draw_static_mesh_renderer_component(object);
        draw_renderable_info_component(object);

        ImGui::End();
    }

    bool Inspector::find_selected_object(GameCore::GameObject& a_outObject)
    {
        bool found = false;
        const GameCore::EntityId selectedEntityId = *m_selectedEntityId;
        const Result result =
            m_gameWorld->for_each_object(
                [&a_outObject, &found, selectedEntityId](
                    GameCore::EntityId a_entityId,
                    GameCore::GameObject a_object)
                {
                    if (found || a_entityId != selectedEntityId)
                    {
                        return;
                    }

                    a_outObject = a_object;
                    found = a_object.is_valid();
                });

        return result && found;
    }

    void Inspector::draw_base_component(GameCore::GameObject& a_object)
    {
        GameCore::BaseComponent* base = nullptr;
        if (!a_object.get_component(base) || base == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("Base", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Name: %s", base->name.c_str());
            ImGui::Text("Tag: %s", base->tag.c_str());
            text_scene_id("OwningScene", base->owningSceneId);
            text_entity_id("Parent", base->parent);
            ImGui::Text("ActiveSelf: %s", base->isActiveSelf ? "true" : "false");
            ImGui::Text("Persistent: %s", base->isPersistent ? "true" : "false");
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

            float position[3] = { edited.position.x, edited.position.y, edited.position.z };
            if (ImGui::DragFloat3("Position", position, 0.05f))
            {
                edited.position = Math::float3(position[0], position[1], position[2]);
                submit_transform_component(a_object.entity_id(), edited);
            }

            float rotation[4] = {
                edited.rotation.x,
                edited.rotation.y,
                edited.rotation.z,
                edited.rotation.w
            };
            if (ImGui::DragFloat4("Rotation", rotation, 0.01f))
            {
                // quaternion は積み重ね編集で長さが崩れやすいため保存前に正規化する
                edited.rotation =
                    Math::Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]).normalize();
                submit_transform_component(a_object.entity_id(), edited);
            }

            float scale[3] = { edited.scale.x, edited.scale.y, edited.scale.z };
            if (ImGui::DragFloat3("Scale", scale, 0.05f))
            {
                edited.scale = Math::float3(scale[0], scale[1], scale[2]);
                submit_transform_component(a_object.entity_id(), edited);
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
            text_quaternion("Rotation", worldTransform->rotation);
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

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ECS::CameraComponent edited = *camera;

            if (ImGui::DragFloat("FovY", &edited.fovY, 0.1f, 1.0f, 179.0f, "%.3f deg"))
            {
                edited.fovY = std::clamp(edited.fovY, 1.0f, 179.0f);
                submit_camera_component(a_object.entity_id(), edited);
            }
            if (ImGui::DragFloat("AspectRatio", &edited.aspectRatio, 0.01f, 0.0f, 8.0f))
            {
                edited.aspectRatio = std::max(0.0f, edited.aspectRatio);
                submit_camera_component(a_object.entity_id(), edited);
            }
            if (ImGui::DragFloat("NearZ", &edited.nearZ, 0.01f, 0.001f, edited.farZ))
            {
                // projection 行列の破綻を避けるため near/far の最小間隔を維持する
                edited.nearZ = std::max(0.001f, edited.nearZ);
                edited.farZ = std::max(edited.nearZ + 0.001f, edited.farZ);
                submit_camera_component(a_object.entity_id(), edited);
            }
            if (ImGui::DragFloat("FarZ", &edited.farZ, 0.1f, edited.nearZ + 0.001f, 100000.0f))
            {
                // UI 入力の順序に依存せず CameraComponent 側の不変条件を保つ
                edited.farZ = std::max(edited.nearZ + 0.001f, edited.farZ);
                submit_camera_component(a_object.entity_id(), edited);
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

        if (ImGui::CollapsingHeader("Mesh Filter", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("ModelName: %s", meshFilter->modelName.c_str());
            ImGui::Text("MeshId: %u", meshFilter->meshId);
        }
    }

    void Inspector::draw_static_mesh_renderer_component(GameCore::GameObject& a_object)
    {
        ECS::StaticMeshRendererComponent* renderer = nullptr;
        if (!a_object.get_component(renderer) || renderer == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("Static Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ECS::StaticMeshRendererComponent edited = *renderer;

            if (ImGui::InputScalar("MaterialId", ImGuiDataType_U32, &edited.materialId))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            const char* renderQueueItems[] = { "Opaque", "Transparent", "Auto" };
            int renderQueueIndex = render_queue_index(edited.renderQueue);
            if (ImGui::Combo("RenderQueue", &renderQueueIndex, renderQueueItems, IM_ARRAYSIZE(renderQueueItems)))
            {
                edited.renderQueue = render_queue_from_index(renderQueueIndex);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            const char* shadowCasterItems[] = { "Solid", "TwoSided" };
            int shadowCasterIndex = shadow_caster_mode_index(edited.shadowCasterMode);
            if (ImGui::Combo("ShadowCaster", &shadowCasterIndex, shadowCasterItems, IM_ARRAYSIZE(shadowCasterItems)))
            {
                edited.shadowCasterMode = shadow_caster_mode_from_index(shadowCasterIndex);
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

            if (ImGui::InputScalar(
                    "OverrideMask",
                    ImGuiDataType_U32,
                    &edited.propertyBlock.overrideMask))
            {
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            float color[4] = {
                edited.propertyBlock.color.x,
                edited.propertyBlock.color.y,
                edited.propertyBlock.color.z,
                edited.propertyBlock.color.w
            };
            if (ImGui::ColorEdit4("Color", color))
            {
                edited.propertyBlock.color =
                    Math::float4(color[0], color[1], color[2], color[3]);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }

            if (ImGui::DragFloat("Shininess", &edited.propertyBlock.shininess, 0.1f, 0.0f, 4096.0f))
            {
                edited.propertyBlock.shininess = std::max(0.0f, edited.propertyBlock.shininess);
                submit_static_mesh_renderer_component(a_object.entity_id(), edited);
            }
            if (ImGui::Checkbox(
                    "UsesReflectionSkybox",
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

    void Inspector::submit_transform_component(
        GameCore::EntityId a_entityId,
        const ECS::TransformComponent& a_component)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Component 変更は GameWorld の整合更新を通すため直接書き換えず command 化する
        (void)m_commandBridge->submit_command(
            make_set_transform_component_command(a_entityId, a_component));
    }

    void Inspector::submit_camera_component(
        GameCore::EntityId a_entityId,
        const ECS::CameraComponent& a_component)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // Camera 更新後の描画 View 再計算を Engine 側の更新経路へ集約する
        (void)m_commandBridge->submit_command(
            make_set_camera_component_command(a_entityId, a_component));
    }

    void Inspector::submit_static_mesh_renderer_component(
        GameCore::EntityId a_entityId,
        const ECS::StaticMeshRendererComponent& a_component)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        // RendererComponent は DrawSystem 抽出に関わるため CQRS 経由で変更順序を揃える
        (void)m_commandBridge->submit_command(
            make_set_static_mesh_renderer_component_command(a_entityId, a_component));
    }
} // namespace Cue::Editor
