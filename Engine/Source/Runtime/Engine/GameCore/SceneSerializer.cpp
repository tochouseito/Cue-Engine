#include "SceneSerializer.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === External includes ===
#include <nlohmann/json.hpp>

// === C++ includes ===
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Cue::GameCore
{
    namespace
    {
        constexpr std::uint32_t k_sceneVersion = 1;

        [[nodiscard]] nlohmann::json make_float3_json(const Math::float3& a_value)
        {
            return nlohmann::json::array({a_value.x, a_value.y, a_value.z});
        }

        [[nodiscard]] nlohmann::json make_float4_json(const Math::float4& a_value)
        {
            return nlohmann::json::array({a_value.x, a_value.y, a_value.z, a_value.w});
        }

        [[nodiscard]] nlohmann::json make_quaternion_json(const Math::Quaternion& a_value)
        {
            return nlohmann::json::array({a_value.x, a_value.y, a_value.z, a_value.w});
        }

        [[nodiscard]] Math::float3 read_float3(
            const nlohmann::json& a_json,
            const Math::float3& a_default)
        {
            if (a_json.is_null())
            {
                return a_default;
            }

            return Math::float3(
                a_json.at(0).get<float>(),
                a_json.at(1).get<float>(),
                a_json.at(2).get<float>());
        }

        [[nodiscard]] Math::float4 read_float4(
            const nlohmann::json& a_json,
            const Math::float4& a_default)
        {
            if (a_json.is_null())
            {
                return a_default;
            }

            return Math::float4(
                a_json.at(0).get<float>(),
                a_json.at(1).get<float>(),
                a_json.at(2).get<float>(),
                a_json.at(3).get<float>());
        }

        [[nodiscard]] Math::Quaternion read_quaternion(
            const nlohmann::json& a_json,
            const Math::Quaternion& a_default)
        {
            if (a_json.is_null())
            {
                return a_default;
            }

            return Math::Quaternion(
                a_json.at(0).get<float>(),
                a_json.at(1).get<float>(),
                a_json.at(2).get<float>(),
                a_json.at(3).get<float>());
        }

        [[nodiscard]] const char* to_string(ECS::RenderQueue a_value) noexcept
        {
            switch (a_value)
            {
            case ECS::RenderQueue::Opaque:
                return "Opaque";
            case ECS::RenderQueue::Transparent:
                return "Transparent";
            case ECS::RenderQueue::Auto:
            default:
                return "Auto";
            }
        }

        [[nodiscard]] ECS::RenderQueue read_render_queue(const nlohmann::json& a_json)
        {
            const std::string value = a_json.is_string() ? a_json.get<std::string>() : std::string("Auto");
            if (value == "Opaque")
            {
                return ECS::RenderQueue::Opaque;
            }
            if (value == "Transparent")
            {
                return ECS::RenderQueue::Transparent;
            }
            return ECS::RenderQueue::Auto;
        }

        [[nodiscard]] const char* to_string(ECS::ShadowCasterMode a_value) noexcept
        {
            switch (a_value)
            {
            case ECS::ShadowCasterMode::TwoSided:
                return "TwoSided";
            case ECS::ShadowCasterMode::Solid:
            default:
                return "Solid";
            }
        }

        [[nodiscard]] ECS::ShadowCasterMode read_shadow_caster_mode(const nlohmann::json& a_json)
        {
            const std::string value = a_json.is_string() ? a_json.get<std::string>() : std::string("Solid");
            if (value == "TwoSided")
            {
                return ECS::ShadowCasterMode::TwoSided;
            }
            return ECS::ShadowCasterMode::Solid;
        }

        [[nodiscard]] nlohmann::json make_transform_json(const SceneTransform& a_transform)
        {
            return nlohmann::json{
                {"position", make_float3_json(a_transform.position)},
                {"rotation", make_quaternion_json(a_transform.rotation)},
                {"scale", make_float3_json(a_transform.scale)}};
        }

        [[nodiscard]] SceneTransform read_transform(const nlohmann::json& a_json)
        {
            SceneTransform transform{};
            transform.position = read_float3(a_json.value("position", nlohmann::json{}), transform.position);
            transform.rotation = read_quaternion(a_json.value("rotation", nlohmann::json{}), transform.rotation);
            transform.scale = read_float3(a_json.value("scale", nlohmann::json{}), transform.scale);
            return transform;
        }

        [[nodiscard]] nlohmann::json make_camera_json(const SceneCamera& a_camera)
        {
            return nlohmann::json{
                {"fovY", a_camera.fovY},
                {"aspectRatio", a_camera.aspectRatio},
                {"nearZ", a_camera.nearZ},
                {"farZ", a_camera.farZ}};
        }

        [[nodiscard]] SceneCamera read_camera(const nlohmann::json& a_json)
        {
            SceneCamera camera{};
            camera.fovY = a_json.value("fovY", camera.fovY);
            camera.aspectRatio = a_json.value("aspectRatio", camera.aspectRatio);
            camera.nearZ = a_json.value("nearZ", camera.nearZ);
            camera.farZ = a_json.value("farZ", camera.farZ);
            return camera;
        }

        [[nodiscard]] nlohmann::json make_renderable_json(const SceneRenderable& a_renderable)
        {
            return nlohmann::json{
                {"modelName", a_renderable.modelName},
                {"meshId", a_renderable.meshId},
                {"materialId", a_renderable.materialId},
                {"renderQueue", to_string(a_renderable.renderQueue)},
                {"shadowCasterMode", to_string(a_renderable.shadowCasterMode)},
                {"visible", a_renderable.visible},
                {"castsShadow", a_renderable.castsShadow},
                {"receivesShadow", a_renderable.receivesShadow},
                {"material", nlohmann::json{
                    {"color", make_float4_json(a_renderable.propertyBlock.color)},
                    {"shininess", a_renderable.propertyBlock.shininess},
                    {"overrideMask", a_renderable.propertyBlock.overrideMask},
                    {"usesReflectionSkybox", a_renderable.propertyBlock.usesReflectionSkybox}}}};
        }

        [[nodiscard]] SceneRenderable read_renderable(const nlohmann::json& a_json)
        {
            SceneRenderable renderable{};
            renderable.modelName = a_json.value("modelName", renderable.modelName);
            renderable.meshId = a_json.value("meshId", renderable.meshId);
            renderable.materialId = a_json.value("materialId", renderable.materialId);
            renderable.renderQueue = read_render_queue(a_json.value("renderQueue", nlohmann::json{}));
            renderable.shadowCasterMode = read_shadow_caster_mode(a_json.value("shadowCasterMode", nlohmann::json{}));
            renderable.visible = a_json.value("visible", renderable.visible);
            renderable.castsShadow = a_json.value("castsShadow", renderable.castsShadow);
            renderable.receivesShadow = a_json.value("receivesShadow", renderable.receivesShadow);

            const nlohmann::json material = a_json.value("material", nlohmann::json::object());
            renderable.propertyBlock.color = read_float4(
                material.value("color", nlohmann::json{}),
                renderable.propertyBlock.color);
            renderable.propertyBlock.shininess = material.value("shininess", renderable.propertyBlock.shininess);
            renderable.propertyBlock.overrideMask = material.value("overrideMask", renderable.propertyBlock.overrideMask);
            renderable.propertyBlock.usesReflectionSkybox =
                material.value("usesReflectionSkybox", renderable.propertyBlock.usesReflectionSkybox);
            return renderable;
        }

        [[nodiscard]] nlohmann::json make_object_json(const SceneObject& a_object)
        {
            nlohmann::json components = nlohmann::json::object();
            if (a_object.hasTransform)
            {
                components["transform"] = make_transform_json(a_object.transform);
            }
            if (a_object.hasCamera)
            {
                components["camera"] = make_camera_json(a_object.camera);
            }
            if (a_object.hasRenderable)
            {
                components["renderable"] = make_renderable_json(a_object.renderable);
            }

            return nlohmann::json{
                {"id", a_object.localId},
                {"parent", a_object.parentLocalId},
                {"name", a_object.name},
                {"tag", a_object.tag},
                {"isActive", a_object.isActive},
                {"isPersistent", a_object.isPersistent},
                {"components", std::move(components)}};
        }

        [[nodiscard]] SceneObject read_object(const nlohmann::json& a_json)
        {
            SceneObject object{};
            object.localId = a_json.value("id", object.localId);
            object.parentLocalId = a_json.value("parent", object.parentLocalId);
            object.name = a_json.value("name", object.name);
            object.tag = a_json.value("tag", object.tag);
            object.isActive = a_json.value("isActive", object.isActive);
            object.isPersistent = a_json.value("isPersistent", object.isPersistent);

            const nlohmann::json components = a_json.value("components", nlohmann::json::object());
            if (components.contains("transform"))
            {
                object.transform = read_transform(components.at("transform"));
                object.hasTransform = true;
            }
            if (components.contains("camera"))
            {
                object.camera = read_camera(components.at("camera"));
                object.hasCamera = true;
            }
            if (components.contains("renderable"))
            {
                object.renderable = read_renderable(components.at("renderable"));
                object.hasRenderable = true;
            }
            return object;
        }

        [[nodiscard]] nlohmann::json make_scene_json(const SceneAsset& a_scene)
        {
            nlohmann::json objects = nlohmann::json::array();
            for (const SceneObject& object : a_scene.objects)
            {
                objects.push_back(make_object_json(object));
            }

            return nlohmann::json{
                {"version", k_sceneVersion},
                {"name", a_scene.name},
                {"objects", std::move(objects)}};
        }

        [[nodiscard]] Result read_scene_json(const nlohmann::json& a_json, SceneAsset& a_outScene)
        {
            const std::uint32_t version = a_json.value("version", 0u);
            if (version != k_sceneVersion)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Scene version is not supported.");
            }

            SceneAsset scene{};
            scene.version = version;
            scene.name = a_json.value("name", scene.name);
            for (const nlohmann::json& objectJson : a_json.value("objects", nlohmann::json::array()))
            {
                scene.objects.push_back(read_object(objectJson));
            }

            a_outScene = std::move(scene);
            return Result::ok();
        }
    } // namespace

    Result load_scene_asset(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_path,
        SceneAsset& a_outScene) noexcept
    {
        a_outScene = {};

        std::vector<std::byte> fileData{};
        Result result = a_fileSystem.read_all(a_path, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const nlohmann::json rootJson = nlohmann::json::parse(text);
            return read_scene_json(rootJson, a_outScene);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Scene file could not be parsed.");
        }
    }

    Result save_scene_asset(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_path,
        const SceneAsset& a_scene) noexcept
    {
        try
        {
            const nlohmann::json rootJson = make_scene_json(a_scene);
            const std::string text = rootJson.dump(4);

            std::vector<std::byte> fileData(text.size());
            if (!text.empty())
            {
                std::memcpy(fileData.data(), text.data(), text.size());
            }

            return a_fileSystem.write_all(a_path, fileData, true);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Scene file could not be written.");
        }
    }
} // namespace Cue::GameCore
