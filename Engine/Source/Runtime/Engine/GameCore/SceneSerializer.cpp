#include "SceneSerializer.h"

// === Engine includes ===
#include "Components.h"

// === C++ includes ===
#include <span>
#include <vector>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::GameCore
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] Json serialize_float3(const Math::float3& a_value)
        {
            return Json{
                { "x", a_value.x },
                { "y", a_value.y },
                { "z", a_value.z },
            };
        }

        void deserialize_float3(const Json& a_json, Math::float3& a_outValue)
        {
            a_outValue.x = a_json.at("x").get<float>();
            a_outValue.y = a_json.at("y").get<float>();
            a_outValue.z = a_json.at("z").get<float>();
        }

        [[nodiscard]] Json serialize_transform(
            const ECS::TransformComponent& a_component)
        {
            return Json{
                { "position", serialize_float3(a_component.position) },
                { "rotation", serialize_float3(a_component.rotation) },
                { "scale", serialize_float3(a_component.scale) },
            };
        }

        void deserialize_transform(
            const Json& a_json, ECS::TransformComponent& a_outComponent)
        {
            deserialize_float3(a_json.at("position"), a_outComponent.position);
            deserialize_float3(a_json.at("rotation"), a_outComponent.rotation);
            deserialize_float3(a_json.at("scale"), a_outComponent.scale);
        }

        [[nodiscard]] Json serialize_camera(const ECS::CameraComponent& a_component)
        {
            return Json{
                { "isMain", a_component.isMain },
                { "fovY", a_component.fovY },
                { "aspectRatio", a_component.aspectRatio },
                { "nearZ", a_component.nearZ },
                { "farZ", a_component.farZ },
            };
        }

        void deserialize_camera(
            const Json& a_json, ECS::CameraComponent& a_outComponent)
        {
            a_outComponent.isMain = a_json.at("isMain").get<bool>();
            a_outComponent.fovY = a_json.at("fovY").get<float>();
            a_outComponent.aspectRatio = a_json.at("aspectRatio").get<float>();
            a_outComponent.nearZ = a_json.at("nearZ").get<float>();
            a_outComponent.farZ = a_json.at("farZ").get<float>();
        }

        [[nodiscard]] Json serialize_mesh_filter(
            const ECS::MeshFilterComponent& a_component)
        {
            return Json{
                { "meshId", a_component.meshId },
            };
        }

        void deserialize_mesh_filter(
            const Json& a_json, ECS::MeshFilterComponent& a_outComponent)
        {
            a_outComponent.meshId = a_json.at("meshId").get<uint32_t>();
        }

        [[nodiscard]] Json serialize_static_mesh_renderer(
            const ECS::StaticMeshRendererComponent& a_component)
        {
            return Json{
                { "materialId", a_component.materialId },
                { "visible", a_component.visible },
            };
        }

        void deserialize_static_mesh_renderer(
            const Json& a_json, ECS::StaticMeshRendererComponent& a_outComponent)
        {
            a_outComponent.materialId = a_json.at("materialId").get<uint32_t>();
            a_outComponent.visible = a_json.at("visible").get<bool>();
        }

        [[nodiscard]] const char* to_string(ECS::ScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case ECS::ScriptFieldType::Float:
                return "Float";
            case ECS::ScriptFieldType::Int32:
                return "Int32";
            case ECS::ScriptFieldType::Bool:
                return "Bool";
            }

            return "Float";
        }

        [[nodiscard]] ECS::ScriptFieldType parse_script_field_type(
            const Json& a_json) noexcept
        {
            const std::string typeName = a_json.get<std::string>();
            if (typeName == "Int32")
            {
                return ECS::ScriptFieldType::Int32;
            }
            if (typeName == "Bool")
            {
                return ECS::ScriptFieldType::Bool;
            }

            return ECS::ScriptFieldType::Float;
        }

        [[nodiscard]] Json serialize_script_field_value(
            const ECS::ScriptFieldValue& a_fieldValue)
        {
            return Json{
                { "name", a_fieldValue.name },
                { "type", to_string(a_fieldValue.type) },
                { "floatValue", a_fieldValue.floatValue },
                { "intValue", a_fieldValue.intValue },
                { "boolValue", a_fieldValue.boolValue },
            };
        }

        void deserialize_script_field_value(
            const Json& a_json,
            ECS::ScriptFieldValue& a_outFieldValue)
        {
            a_outFieldValue.name = a_json.value("name", std::string{});
            a_outFieldValue.type =
                parse_script_field_type(a_json.value("type", std::string("Float")));
            a_outFieldValue.floatValue = a_json.value("floatValue", 0.0f);
            a_outFieldValue.intValue = a_json.value("intValue", 0);
            a_outFieldValue.boolValue = a_json.value("boolValue", false);
        }

        [[nodiscard]] Json serialize_script(
            const ECS::ScriptComponent& a_component)
        {
            Json fieldValues = Json::array();
            for (const ECS::ScriptFieldValue& fieldValue : a_component.fieldValues)
            {
                fieldValues.push_back(serialize_script_field_value(fieldValue));
            }

            return Json{
                { "className", a_component.className },
                { "isEnabled", a_component.isEnabled },
                { "fieldValues", std::move(fieldValues) },
            };
        }

        void deserialize_script(
            const Json& a_json, ECS::ScriptComponent& a_outComponent)
        {
            a_outComponent.className =
                a_json.value("className", std::string{});
            a_outComponent.isEnabled =
                a_json.value("isEnabled", true);
            a_outComponent.fieldValues.clear();

            const Json fieldValuesJson =
                a_json.value("fieldValues", Json::array());
            if (!fieldValuesJson.is_array())
            {
                return;
            }

            a_outComponent.fieldValues.reserve(fieldValuesJson.size());
            for (const Json& fieldValueJson : fieldValuesJson)
            {
                ECS::ScriptFieldValue fieldValue{};
                deserialize_script_field_value(fieldValueJson, fieldValue);
                a_outComponent.fieldValues.push_back(std::move(fieldValue));
            }
        }

        [[nodiscard]] Json serialize_object_definition(
            const ObjectDefinition& a_definition)
        {
            Json objectJson = {
                { "localObjectId", a_definition.localObjectId },
                { "isActive", a_definition.isActive },
                { "isPersistent", a_definition.isPersistent },
                { "name", a_definition.name() },
                { "tag", a_definition.tag() },
            };

            if (a_definition.parentLocalObjectId.has_value())
            {
                objectJson["parentLocalObjectId"] = *a_definition.parentLocalObjectId;
            }

            Json componentsJson = Json::object();

            if (const ECS::TransformComponent* transform =
                a_definition.prototype.get_component_ptr<ECS::TransformComponent>();
                transform != nullptr)
            {
                componentsJson["transform"] = serialize_transform(*transform);
            }

            if (const ECS::CameraComponent* camera =
                a_definition.prototype.get_component_ptr<ECS::CameraComponent>();
                camera != nullptr)
            {
                componentsJson["camera"] = serialize_camera(*camera);
            }

            if (const ECS::MeshFilterComponent* meshFilter =
                a_definition.prototype.get_component_ptr<ECS::MeshFilterComponent>();
                meshFilter != nullptr)
            {
                componentsJson["meshFilter"] = serialize_mesh_filter(*meshFilter);
            }

            if (const ECS::StaticMeshRendererComponent* renderer =
                a_definition.prototype.get_component_ptr<ECS::StaticMeshRendererComponent>();
                renderer != nullptr)
            {
                componentsJson["staticMeshRenderer"] =
                    serialize_static_mesh_renderer(*renderer);
            }

            if (const ECS::ScriptComponent* script =
                a_definition.prototype.get_component_ptr<ECS::ScriptComponent>();
                script != nullptr)
            {
                componentsJson["script"] = serialize_script(*script);
            }

            objectJson["components"] = std::move(componentsJson);
            return objectJson;
        }

        [[nodiscard]] Result deserialize_object_definition(
            const Json& a_json, ObjectDefinition& a_outDefinition) noexcept
        {
            try
            {
                ObjectDefinition objectDefinition{};
                objectDefinition.localObjectId =
                    a_json.at("localObjectId").get<LocalObjectId>();
                objectDefinition.isActive = a_json.value("isActive", true);
                objectDefinition.isPersistent = a_json.value("isPersistent", false);

                if (const Json::const_iterator parentIt =
                    a_json.find("parentLocalObjectId");
                    parentIt != a_json.end() && !parentIt->is_null())
                {
                    objectDefinition.parentLocalObjectId =
                        parentIt->get<LocalObjectId>();
                }

                const std::string objectName =
                    a_json.at("name").get<std::string>();
                const std::string objectTag =
                    a_json.value("tag", std::string("Default"));
                objectDefinition.prototype =
                    GameObjectProto(objectName, objectTag);

                const Json& componentsJson =
                    a_json.value("components", Json::object());

                if (const Json::const_iterator transformIt =
                    componentsJson.find("transform");
                    transformIt != componentsJson.end())
                {
                    ECS::TransformComponent transform{};
                    deserialize_transform(*transformIt, transform);
                    objectDefinition.prototype.add_component(transform);
                }

                if (const Json::const_iterator cameraIt =
                    componentsJson.find("camera");
                    cameraIt != componentsJson.end())
                {
                    ECS::CameraComponent camera{};
                    deserialize_camera(*cameraIt, camera);
                    objectDefinition.prototype.add_component(camera);
                }

                if (const Json::const_iterator meshFilterIt =
                    componentsJson.find("meshFilter");
                    meshFilterIt != componentsJson.end())
                {
                    ECS::MeshFilterComponent meshFilter{};
                    deserialize_mesh_filter(*meshFilterIt, meshFilter);
                    objectDefinition.prototype.add_component(meshFilter);
                }

                if (const Json::const_iterator rendererIt =
                    componentsJson.find("staticMeshRenderer");
                    rendererIt != componentsJson.end())
                {
                    ECS::StaticMeshRendererComponent renderer{};
                    deserialize_static_mesh_renderer(*rendererIt, renderer);
                    objectDefinition.prototype.add_component(renderer);
                }

                if (const Json::const_iterator scriptIt =
                    componentsJson.find("script");
                    scriptIt != componentsJson.end())
                {
                    ECS::ScriptComponent script{};
                    deserialize_script(*scriptIt, script);
                    objectDefinition.prototype.add_component(script);
                }

                a_outDefinition = std::move(objectDefinition);
                return Result::ok();
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Scene object definition could not be parsed.");
            }
        }
    }

    Result SceneSerializer::save_scene_asset(const SceneAsset& a_sceneAsset,
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_filePath) noexcept
    {
        try
        {
            Json root = {
                { "version", k_currentVersion },
                { "name", a_sceneAsset.name() },
                { "objects", Json::array() },
            };

            for (const ObjectDefinition& object : a_sceneAsset.objects())
            {
                root["objects"].push_back(serialize_object_definition(object));
            }

            std::string text = root.dump(4);
            text.push_back('\n');

            const std::span<const char> charSpan(text.data(), text.size());
            const std::span<const std::byte> byteSpan = std::as_bytes(charSpan);
            return a_fileSystem.write_all(a_filePath, byteSpan, true);
        }
        catch (...)
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Scene asset could not be serialized.");
        }
    }

    Result SceneSerializer::load_scene_asset(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_filePath,
        SceneAsset& a_outSceneAsset) noexcept
    {
        std::vector<std::byte> fileData{};
        Result result = a_fileSystem.read_all(a_filePath, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const Json root = Json::parse(text);

            const uint32_t version = root.at("version").get<uint32_t>();
            if (version != k_currentVersion)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Scene asset version is not supported.");
            }

            SceneAsset sceneAsset(root.at("name").get<std::string>());
            const Json& objectsJson = root.at("objects");
            if (!objectsJson.is_array())
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Scene asset objects must be an array.");
            }

            for (const Json& objectJson : objectsJson)
            {
                ObjectDefinition objectDefinition{};
                result = deserialize_object_definition(objectJson, objectDefinition);
                if (!result)
                {
                    return result;
                }

                sceneAsset.add_object(std::move(objectDefinition));
            }

            a_outSceneAsset = std::move(sceneAsset);
            return Result::ok();
        }
        catch (...)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Scene asset could not be deserialized.");
        }
    }
}
