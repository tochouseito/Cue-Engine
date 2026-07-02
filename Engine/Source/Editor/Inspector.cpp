#include "Inspector.h"

// === Runtime includes ===
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <string>

// === ImGui includes ===
#include <imgui.h>

namespace
{
    const char* render_queue_name(Cue::ECS::RenderQueue a_queue) noexcept
    {
        switch (a_queue)
        {
        case Cue::ECS::RenderQueue::Opaque:
            return "Opaque";
        case Cue::ECS::RenderQueue::Transparent:
            return "Transparent";
        case Cue::ECS::RenderQueue::Auto:
        default:
            return "Auto";
        }
    }

    const char* shadow_caster_mode_name(Cue::ECS::ShadowCasterMode a_mode) noexcept
    {
        switch (a_mode)
        {
        case Cue::ECS::ShadowCasterMode::TwoSided:
            return "TwoSided";
        case Cue::ECS::ShadowCasterMode::Solid:
        default:
            return "Solid";
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
    Inspector::Inspector(GameCore::GameWorld* a_gameWorld,
                         GameCore::EntityId* a_selectedEntityId) noexcept
        : m_gameWorld(a_gameWorld),
          m_selectedEntityId(a_selectedEntityId)
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
            text_float3("Position", transform->position);
            text_quaternion("Rotation", transform->rotation);
            text_float3("Scale", transform->scale);
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
            ImGui::Text("FovY: %.3f", camera->fovY);
            ImGui::Text("AspectRatio: %.3f", camera->aspectRatio);
            ImGui::Text("NearZ: %.3f", camera->nearZ);
            ImGui::Text("FarZ: %.3f", camera->farZ);
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
            ImGui::Text("MaterialId: %u", renderer->materialId);
            ImGui::Text("RenderQueue: %s", render_queue_name(renderer->renderQueue));
            ImGui::Text("ShadowCaster: %s", shadow_caster_mode_name(renderer->shadowCasterMode));
            ImGui::Text("Visible: %s", renderer->visible ? "true" : "false");
            ImGui::Text("CastsShadow: %s", renderer->castsShadow ? "true" : "false");
            ImGui::Text("ReceivesShadow: %s", renderer->receivesShadow ? "true" : "false");
            ImGui::Text("OverrideMask: %u", renderer->propertyBlock.overrideMask);
            ImGui::Text("Color: %.3f, %.3f, %.3f, %.3f",
                renderer->propertyBlock.color.x,
                renderer->propertyBlock.color.y,
                renderer->propertyBlock.color.z,
                renderer->propertyBlock.color.w);
            ImGui::Text("Shininess: %.3f", renderer->propertyBlock.shininess);
            ImGui::Text("UsesReflectionSkybox: %s",
                renderer->propertyBlock.usesReflectionSkybox ? "true" : "false");
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
} // namespace Cue::Editor
