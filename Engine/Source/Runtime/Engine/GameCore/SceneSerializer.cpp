#include "SceneSerializer.h"

// === Engine includes ===
#include "Components.h"
#include "Navigation/NavComponents.h"

// === C++ includes ===
#include <algorithm>
#include <span>
#include <vector>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::GameCore
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] Json serialize_float2(const Math::float2& a_value)
        {
            return Json{
                { "x", a_value.x },
                { "y", a_value.y },
            };
        }

        void deserialize_float2(const Json& a_json, Math::float2& a_outValue)
        {
            a_outValue.x = a_json.at("x").get<float>();
            a_outValue.y = a_json.at("y").get<float>();
        }

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

        [[nodiscard]] Json serialize_float4(const Math::float4& a_value)
        {
            return Json{
                { "x", a_value.x },
                { "y", a_value.y },
                { "z", a_value.z },
                { "w", a_value.w },
            };
        }

        void deserialize_float4(const Json& a_json, Math::float4& a_outValue)
        {
            a_outValue.x = a_json.at("x").get<float>();
            a_outValue.y = a_json.at("y").get<float>();
            a_outValue.z = a_json.at("z").get<float>();
            a_outValue.w = a_json.at("w").get<float>();
        }

        [[nodiscard]] Json serialize_quaternion(
            const Math::Quaternion& a_value)
        {
            return Json{
                { "x", a_value.x },
                { "y", a_value.y },
                { "z", a_value.z },
                { "w", a_value.w },
            };
        }

        void deserialize_quaternion(
            const Json& a_json,
            Math::Quaternion& a_outValue)
        {
            a_outValue.x = a_json.at("x").get<float>();
            a_outValue.y = a_json.at("y").get<float>();
            a_outValue.z = a_json.at("z").get<float>();
            a_outValue.w = a_json.at("w").get<float>();
            a_outValue.normalize();
        }

        [[nodiscard]] const char* to_string(
            ECS::ShadowCasterMode a_mode) noexcept
        {
            switch (a_mode)
            {
            case ECS::ShadowCasterMode::Solid:
                return "Solid";
            case ECS::ShadowCasterMode::TwoSided:
                return "TwoSided";
            default:
                return "Solid";
            }
        }

        [[nodiscard]] ECS::ShadowCasterMode shadow_caster_mode_from_string(
            const std::string& a_value) noexcept
        {
            if (a_value == "TwoSided")
            {
                return ECS::ShadowCasterMode::TwoSided;
            }

            return ECS::ShadowCasterMode::Solid;
        }

        [[nodiscard]] const char* to_string(ECS::RenderQueue a_queue) noexcept
        {
            switch (a_queue)
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

        [[nodiscard]] ECS::RenderQueue render_queue_from_string(
            const std::string& a_value) noexcept
        {
            if (a_value == "Opaque")
            {
                return ECS::RenderQueue::Opaque;
            }
            if (a_value == "Transparent")
            {
                return ECS::RenderQueue::Transparent;
            }

            return ECS::RenderQueue::Auto;
        }

        [[nodiscard]] const char* to_string(
            Physics::MotionType a_type) noexcept
        {
            switch (a_type)
            {
            case Physics::MotionType::Static:
                return "Static";
            case Physics::MotionType::Kinematic:
                return "Kinematic";
            case Physics::MotionType::Dynamic:
                return "Dynamic";
            }

            return "Static";
        }

        [[nodiscard]] Physics::MotionType parse_motion_type(
            const Json& a_json) noexcept
        {
            const std::string typeName = a_json.get<std::string>();
            if (typeName == "Kinematic")
            {
                return Physics::MotionType::Kinematic;
            }
            if (typeName == "Dynamic")
            {
                return Physics::MotionType::Dynamic;
            }

            return Physics::MotionType::Static;
        }

        [[nodiscard]] const char* to_string(
            Physics::ShapeType a_type) noexcept
        {
            switch (a_type)
            {
            case Physics::ShapeType::Box:
                return "Box";
            case Physics::ShapeType::Sphere:
                return "Sphere";
            case Physics::ShapeType::Capsule:
                return "Capsule";
            case Physics::ShapeType::Mesh:
                return "Mesh";
            }

            return "Box";
        }

        [[nodiscard]] Physics::ShapeType parse_shape_type(
            const Json& a_json) noexcept
        {
            const std::string typeName = a_json.get<std::string>();
            if (typeName == "Sphere")
            {
                return Physics::ShapeType::Sphere;
            }
            if (typeName == "Capsule")
            {
                return Physics::ShapeType::Capsule;
            }
            if (typeName == "Mesh")
            {
                return Physics::ShapeType::Mesh;
            }

            return Physics::ShapeType::Box;
        }

        [[nodiscard]] const char* to_string(
            ECS::ParticleBillboardMode a_mode) noexcept
        {
            switch (a_mode)
            {
            case ECS::ParticleBillboardMode::View:
                return "View";
            }

            return "View";
        }

        [[nodiscard]] ECS::ParticleBillboardMode parse_particle_billboard_mode(
            const Json& a_json) noexcept
        {
            const std::string modeName = a_json.get<std::string>();
            if (modeName == "View")
            {
                return ECS::ParticleBillboardMode::View;
            }

            return ECS::ParticleBillboardMode::View;
        }

        [[nodiscard]] Json serialize_transform(
            const ECS::TransformComponent& a_component)
        {
            return Json{
                { "position", serialize_float3(a_component.position) },
                { "rotation", serialize_float3(
                    Math::quaternion_to_euler_xyz(a_component.rotation)) },
                { "rotationQuaternion",
                    serialize_quaternion(a_component.rotation) },
                { "scale", serialize_float3(a_component.scale) },
            };
        }

        void deserialize_transform(
            const Json& a_json, ECS::TransformComponent& a_outComponent)
        {
            deserialize_float3(a_json.at("position"), a_outComponent.position);
            if (const Json::const_iterator rotationQuaternionIt =
                a_json.find("rotationQuaternion");
                rotationQuaternionIt != a_json.end())
            {
                deserialize_quaternion(
                    *rotationQuaternionIt,
                    a_outComponent.rotation);
            }
            else
            {
                Math::float3 rotationRadians = Math::float3::zero();
                deserialize_float3(a_json.at("rotation"), rotationRadians);
                a_outComponent.rotation =
                    Math::quaternion_from_euler_xyz(rotationRadians);
            }
            deserialize_float3(a_json.at("scale"), a_outComponent.scale);
        }

        [[nodiscard]] const char* to_string(
            ECS::NavAgentMovementMode a_mode) noexcept
        {
            switch (a_mode)
            {
            case ECS::NavAgentMovementMode::DesiredVelocityOnly:
                return "DesiredVelocityOnly";
            case ECS::NavAgentMovementMode::DirectTransform:
            default:
                return "DirectTransform";
            }
        }

        [[nodiscard]] ECS::NavAgentMovementMode parse_nav_agent_movement_mode(
            const Json& a_json) noexcept
        {
            const std::string modeName = a_json.get<std::string>();
            if (modeName == "DesiredVelocityOnly")
            {
                return ECS::NavAgentMovementMode::DesiredVelocityOnly;
            }

            return ECS::NavAgentMovementMode::DirectTransform;
        }

        [[nodiscard]] Json serialize_nav_agent(
            const ECS::NavAgentComponent& a_component)
        {
            return Json{
                { "radius", a_component.radius },
                { "height", a_component.height },
                { "maxSpeed", a_component.maxSpeed },
                { "acceleration", a_component.acceleration },
                { "stoppingDistance", a_component.stoppingDistance },
                { "navMeshSnapDistance", a_component.navMeshSnapDistance },
                { "includeFlags", a_component.includeFlags },
                { "excludeFlags", a_component.excludeFlags },
                { "destination", serialize_float3(a_component.destination) },
                { "movementMode", to_string(a_component.movementMode) },
                { "shouldSnapToNavMesh", a_component.shouldSnapToNavMesh },
                { "hasDestination", a_component.hasDestination },
            };
        }

        void deserialize_nav_agent(
            const Json& a_json, ECS::NavAgentComponent& a_outComponent)
        {
            a_outComponent.radius = a_json.value("radius", a_outComponent.radius);
            a_outComponent.height = a_json.value("height", a_outComponent.height);
            a_outComponent.maxSpeed =
                a_json.value("maxSpeed", a_outComponent.maxSpeed);
            a_outComponent.acceleration =
                a_json.value("acceleration", a_outComponent.acceleration);
            a_outComponent.stoppingDistance =
                a_json.value("stoppingDistance",
                    a_outComponent.stoppingDistance);
            a_outComponent.navMeshSnapDistance =
                a_json.value("navMeshSnapDistance",
                    a_outComponent.navMeshSnapDistance);
            a_outComponent.includeFlags =
                a_json.value("includeFlags", a_outComponent.includeFlags);
            a_outComponent.excludeFlags =
                a_json.value("excludeFlags", a_outComponent.excludeFlags);
            if (const Json::const_iterator movementModeIt =
                a_json.find("movementMode");
                movementModeIt != a_json.end())
            {
                a_outComponent.movementMode =
                    parse_nav_agent_movement_mode(*movementModeIt);
            }
            if (const Json::const_iterator destinationIt =
                a_json.find("destination");
                destinationIt != a_json.end())
            {
                deserialize_float3(*destinationIt, a_outComponent.destination);
            }
            a_outComponent.shouldSnapToNavMesh =
                a_json.value("shouldSnapToNavMesh",
                    a_outComponent.shouldSnapToNavMesh);
            a_outComponent.hasDestination =
                a_json.value("hasDestination", a_outComponent.hasDestination);
            a_outComponent.lastRequestedDestination = Math::float3::zero();
            a_outComponent.desiredVelocity = Math::float3::zero();
            a_outComponent.pathPoints.clear();
            a_outComponent.pathIndex = 0;
            a_outComponent.hasPath = false;
            a_outComponent.hasArrived = false;
            a_outComponent.hasPathFailed = false;
            a_outComponent.isOnNavMesh = false;
        }

        [[nodiscard]] Json serialize_nav_mesh_bake_source(
            const ECS::NavMeshBakeSourceComponent& a_component)
        {
            return Json{
                { "area", static_cast<uint32_t>(a_component.area) },
                { "isIncluded", a_component.isIncluded },
            };
        }

        void deserialize_nav_mesh_bake_source(
            const Json& a_json,
            ECS::NavMeshBakeSourceComponent& a_outComponent)
        {
            a_outComponent.area = static_cast<uint8_t>(
                a_json.value("area", static_cast<uint32_t>(a_outComponent.area)));
            a_outComponent.isIncluded =
                a_json.value("isIncluded", a_outComponent.isIncluded);
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

        [[nodiscard]] Json serialize_canvas(
            const ECS::CanvasComponent& a_component)
        {
            return Json{
                { "referenceSize", serialize_float2(a_component.referenceSize) },
                { "scaleFactor", a_component.scaleFactor },
                { "sortOrder", a_component.sortOrder },
                { "matchesScreen", a_component.matchesScreen },
            };
        }

        void deserialize_canvas(
            const Json& a_json,
            ECS::CanvasComponent& a_outComponent)
        {
            if (const Json::const_iterator sizeIt =
                    a_json.find("referenceSize");
                sizeIt != a_json.end())
            {
                deserialize_float2(*sizeIt, a_outComponent.referenceSize);
            }
            a_outComponent.scaleFactor =
                a_json.value("scaleFactor", a_outComponent.scaleFactor);
            a_outComponent.sortOrder =
                a_json.value("sortOrder", a_outComponent.sortOrder);
            a_outComponent.matchesScreen =
                a_json.value("matchesScreen", a_outComponent.matchesScreen);
        }

        [[nodiscard]] Json serialize_ui_rect_transform(
            const ECS::UiRectTransformComponent& a_component)
        {
            return Json{
                { "anchorMin", serialize_float2(a_component.anchorMin) },
                { "anchorMax", serialize_float2(a_component.anchorMax) },
                { "pivot", serialize_float2(a_component.pivot) },
                { "anchoredPosition",
                    serialize_float2(a_component.anchoredPosition) },
                { "sizeDelta", serialize_float2(a_component.sizeDelta) },
            };
        }

        void deserialize_ui_rect_transform(
            const Json& a_json,
            ECS::UiRectTransformComponent& a_outComponent)
        {
            if (const Json::const_iterator valueIt = a_json.find("anchorMin");
                valueIt != a_json.end())
            {
                deserialize_float2(*valueIt, a_outComponent.anchorMin);
            }
            if (const Json::const_iterator valueIt = a_json.find("anchorMax");
                valueIt != a_json.end())
            {
                deserialize_float2(*valueIt, a_outComponent.anchorMax);
            }
            if (const Json::const_iterator valueIt = a_json.find("pivot");
                valueIt != a_json.end())
            {
                deserialize_float2(*valueIt, a_outComponent.pivot);
            }
            if (const Json::const_iterator valueIt =
                    a_json.find("anchoredPosition");
                valueIt != a_json.end())
            {
                deserialize_float2(*valueIt, a_outComponent.anchoredPosition);
            }
            if (const Json::const_iterator valueIt = a_json.find("sizeDelta");
                valueIt != a_json.end())
            {
                deserialize_float2(*valueIt, a_outComponent.sizeDelta);
            }
            a_outComponent.resolvedMin = Math::float2(0.0f, 0.0f);
            a_outComponent.resolvedSize = Math::float2(0.0f, 0.0f);
            a_outComponent.isResolved = false;
        }

        [[nodiscard]] const char* to_string(
            ECS::UiLayoutDirection a_direction) noexcept
        {
            switch (a_direction)
            {
            case ECS::UiLayoutDirection::Horizontal:
                return "Horizontal";
            case ECS::UiLayoutDirection::Vertical:
                return "Vertical";
            }

            return "Horizontal";
        }

        [[nodiscard]] ECS::UiLayoutDirection ui_layout_direction_from_string(
            const std::string& a_value) noexcept
        {
            if (a_value == "Vertical")
            {
                return ECS::UiLayoutDirection::Vertical;
            }

            return ECS::UiLayoutDirection::Horizontal;
        }

        [[nodiscard]] Json serialize_ui_layout_group(
            const ECS::UiLayoutGroupComponent& a_component)
        {
            return Json{
                { "padding", serialize_float4(a_component.padding) },
                { "spacing", a_component.spacing },
                { "direction", to_string(a_component.direction) },
                { "controlsChildSize", a_component.controlsChildSize },
            };
        }

        void deserialize_ui_layout_group(
            const Json& a_json,
            ECS::UiLayoutGroupComponent& a_outComponent)
        {
            if (const Json::const_iterator paddingIt = a_json.find("padding");
                paddingIt != a_json.end())
            {
                deserialize_float4(*paddingIt, a_outComponent.padding);
            }
            a_outComponent.spacing =
                a_json.value("spacing", a_outComponent.spacing);
            a_outComponent.direction = ui_layout_direction_from_string(
                a_json.value("direction",
                    std::string(to_string(a_outComponent.direction))));
            a_outComponent.controlsChildSize =
                a_json.value("controlsChildSize",
                    a_outComponent.controlsChildSize);
        }

        [[nodiscard]] const char* to_string(
            ECS::TextHorizontalAlign a_align) noexcept
        {
            switch (a_align)
            {
            case ECS::TextHorizontalAlign::Center:
                return "Center";
            case ECS::TextHorizontalAlign::Right:
                return "Right";
            case ECS::TextHorizontalAlign::Left:
            default:
                return "Left";
            }
        }

        [[nodiscard]] ECS::TextHorizontalAlign text_horizontal_align_from_string(
            const std::string& a_value) noexcept
        {
            if (a_value == "Center")
            {
                return ECS::TextHorizontalAlign::Center;
            }
            if (a_value == "Right")
            {
                return ECS::TextHorizontalAlign::Right;
            }
            return ECS::TextHorizontalAlign::Left;
        }

        [[nodiscard]] const char* to_string(
            ECS::TextVerticalAlign a_align) noexcept
        {
            switch (a_align)
            {
            case ECS::TextVerticalAlign::Middle:
                return "Middle";
            case ECS::TextVerticalAlign::Bottom:
                return "Bottom";
            case ECS::TextVerticalAlign::Top:
            default:
                return "Top";
            }
        }

        [[nodiscard]] ECS::TextVerticalAlign text_vertical_align_from_string(
            const std::string& a_value) noexcept
        {
            if (a_value == "Middle")
            {
                return ECS::TextVerticalAlign::Middle;
            }
            if (a_value == "Bottom")
            {
                return ECS::TextVerticalAlign::Bottom;
            }
            return ECS::TextVerticalAlign::Top;
        }

        [[nodiscard]] Json serialize_text_renderer(
            const ECS::TextRendererComponent& a_component)
        {
            return Json{
                { "text", a_component.text },
                { "fontPath", a_component.fontPath },
                { "color", serialize_float4(a_component.color) },
                { "fontSize", a_component.fontSize },
                { "layer", a_component.layer },
                { "order", a_component.order },
                { "horizontalAlign", to_string(a_component.horizontalAlign) },
                { "verticalAlign", to_string(a_component.verticalAlign) },
                { "visible", a_component.visible },
            };
        }

        void deserialize_text_renderer(
            const Json& a_json,
            ECS::TextRendererComponent& a_outComponent)
        {
            a_outComponent.text = a_json.value("text", a_outComponent.text);
            a_outComponent.fontPath =
                a_json.value("fontPath", a_outComponent.fontPath);
            if (const Json::const_iterator colorIt = a_json.find("color");
                colorIt != a_json.end())
            {
                deserialize_float4(*colorIt, a_outComponent.color);
            }
            a_outComponent.fontSize =
                a_json.value("fontSize", a_outComponent.fontSize);
            a_outComponent.layer =
                a_json.value("layer", a_outComponent.layer);
            a_outComponent.order =
                a_json.value("order", a_outComponent.order);
            a_outComponent.horizontalAlign = text_horizontal_align_from_string(
                a_json.value(
                    "horizontalAlign",
                    std::string(to_string(a_outComponent.horizontalAlign))));
            a_outComponent.verticalAlign = text_vertical_align_from_string(
                a_json.value(
                    "verticalAlign",
                    std::string(to_string(a_outComponent.verticalAlign))));
            a_outComponent.visible =
                a_json.value("visible", a_outComponent.visible);
        }

        [[nodiscard]] Json serialize_directional_light(
            const ECS::DirectionalLightComponent& a_component)
        {
            return Json{
                { "color", serialize_float3(a_component.color) },
                { "intensity", a_component.intensity },
                { "shadowBias", a_component.shadowBias },
                { "shadowSlopeBias", a_component.shadowSlopeBias },
                { "shadowSize", a_component.shadowSize },
                { "shadowDistance", a_component.shadowDistance },
                { "shadowStrength", a_component.shadowStrength },
                { "shadowSoftness", a_component.shadowSoftness },
                { "isEnabled", a_component.isEnabled },
                { "castsShadow", a_component.castsShadow },
            };
        }

        void deserialize_directional_light(
            const Json& a_json,
            ECS::DirectionalLightComponent& a_outComponent)
        {
            if (const Json::const_iterator colorIt = a_json.find("color");
                colorIt != a_json.end())
            {
                deserialize_float3(*colorIt, a_outComponent.color);
            }
            a_outComponent.intensity =
                a_json.value("intensity", a_outComponent.intensity);
            a_outComponent.shadowBias =
                a_json.value("shadowBias", a_outComponent.shadowBias);
            a_outComponent.shadowSlopeBias =
                a_json.value("shadowSlopeBias",
                    a_outComponent.shadowSlopeBias);
            a_outComponent.shadowSize =
                a_json.value("shadowSize", a_outComponent.shadowSize);
            a_outComponent.shadowDistance =
                a_json.value("shadowDistance", a_outComponent.shadowDistance);
            a_outComponent.shadowStrength =
                a_json.value("shadowStrength", a_outComponent.shadowStrength);
            a_outComponent.shadowSoftness =
                a_json.value("shadowSoftness", a_outComponent.shadowSoftness);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
            a_outComponent.castsShadow =
                a_json.value("castsShadow", a_outComponent.castsShadow);
        }

        [[nodiscard]] Json serialize_point_light(
            const ECS::PointLightComponent& a_component)
        {
            return Json{
                { "color", serialize_float3(a_component.color) },
                { "intensity", a_component.intensity },
                { "range", a_component.range },
                { "shadowBias", a_component.shadowBias },
                { "shadowSlopeBias", a_component.shadowSlopeBias },
                { "shadowNearClip", a_component.shadowNearClip },
                { "shadowStrength", a_component.shadowStrength },
                { "shadowSoftness", a_component.shadowSoftness },
                { "isEnabled", a_component.isEnabled },
                { "castsShadow", a_component.castsShadow },
            };
        }

        void deserialize_point_light(
            const Json& a_json,
            ECS::PointLightComponent& a_outComponent)
        {
            if (const Json::const_iterator colorIt = a_json.find("color");
                colorIt != a_json.end())
            {
                deserialize_float3(*colorIt, a_outComponent.color);
            }
            a_outComponent.intensity =
                a_json.value("intensity", a_outComponent.intensity);
            a_outComponent.range = a_json.value("range", a_outComponent.range);
            a_outComponent.shadowBias =
                a_json.value("shadowBias", a_outComponent.shadowBias);
            a_outComponent.shadowSlopeBias =
                a_json.value("shadowSlopeBias",
                    a_outComponent.shadowSlopeBias);
            a_outComponent.shadowNearClip =
                a_json.value("shadowNearClip", a_outComponent.shadowNearClip);
            a_outComponent.shadowStrength =
                a_json.value("shadowStrength", a_outComponent.shadowStrength);
            a_outComponent.shadowSoftness =
                a_json.value("shadowSoftness", a_outComponent.shadowSoftness);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
            a_outComponent.castsShadow =
                a_json.value("castsShadow", a_outComponent.castsShadow);
        }

        [[nodiscard]] Json serialize_spot_light(
            const ECS::SpotLightComponent& a_component)
        {
            return Json{
                { "color", serialize_float3(a_component.color) },
                { "intensity", a_component.intensity },
                { "range", a_component.range },
                { "outerAngleDegrees", a_component.outerAngleDegrees },
                { "shadowBias", a_component.shadowBias },
                { "shadowSlopeBias", a_component.shadowSlopeBias },
                { "shadowNearClip", a_component.shadowNearClip },
                { "shadowStrength", a_component.shadowStrength },
                { "shadowSoftness", a_component.shadowSoftness },
                { "isEnabled", a_component.isEnabled },
                { "castsShadow", a_component.castsShadow },
            };
        }

        void deserialize_spot_light(
            const Json& a_json,
            ECS::SpotLightComponent& a_outComponent)
        {
            if (const Json::const_iterator colorIt = a_json.find("color");
                colorIt != a_json.end())
            {
                deserialize_float3(*colorIt, a_outComponent.color);
            }
            a_outComponent.intensity =
                a_json.value("intensity", a_outComponent.intensity);
            a_outComponent.range = a_json.value("range", a_outComponent.range);
            a_outComponent.outerAngleDegrees =
                a_json.value("outerAngleDegrees",
                    a_outComponent.outerAngleDegrees);
            a_outComponent.shadowBias =
                a_json.value("shadowBias", a_outComponent.shadowBias);
            a_outComponent.shadowSlopeBias =
                a_json.value("shadowSlopeBias",
                    a_outComponent.shadowSlopeBias);
            a_outComponent.shadowNearClip =
                a_json.value("shadowNearClip", a_outComponent.shadowNearClip);
            a_outComponent.shadowStrength =
                a_json.value("shadowStrength", a_outComponent.shadowStrength);
            a_outComponent.shadowSoftness =
                a_json.value("shadowSoftness", a_outComponent.shadowSoftness);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
            a_outComponent.castsShadow =
                a_json.value("castsShadow", a_outComponent.castsShadow);
        }

        [[nodiscard]] Json serialize_first_person_camera_controller(
            const ECS::FirstPersonCameraControllerComponent& a_component)
        {
            return Json{
                { "targetEntity", a_component.targetEntity },
                { "offset", serialize_float3(a_component.offset) },
                { "yaw", a_component.yaw },
                { "pitch", a_component.pitch },
                { "mouseSensitivity", a_component.mouseSensitivity },
                { "minPitch", a_component.minPitch },
                { "maxPitch", a_component.maxPitch },
                { "fovY", a_component.fovY },
                { "isEnabled", a_component.isEnabled },
                { "rotatesTargetYaw", a_component.rotatesTargetYaw },
                { "followsTarget", a_component.followsTarget },
            };
        }

        void deserialize_first_person_camera_controller(
            const Json& a_json,
            ECS::FirstPersonCameraControllerComponent& a_outComponent)
        {
            a_outComponent.targetEntity =
                a_json.value("targetEntity", k_invalidEntityId);
            if (const Json::const_iterator offsetIt = a_json.find("offset");
                offsetIt != a_json.end())
            {
                deserialize_float3(*offsetIt, a_outComponent.offset);
            }
            a_outComponent.yaw = a_json.value("yaw", a_outComponent.yaw);
            a_outComponent.pitch = a_json.value("pitch", a_outComponent.pitch);
            a_outComponent.mouseSensitivity =
                a_json.value("mouseSensitivity", a_outComponent.mouseSensitivity);
            a_outComponent.minPitch =
                a_json.value("minPitch", a_outComponent.minPitch);
            a_outComponent.maxPitch =
                a_json.value("maxPitch", a_outComponent.maxPitch);
            a_outComponent.fovY = a_json.value("fovY", a_outComponent.fovY);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
            a_outComponent.rotatesTargetYaw =
                a_json.value("rotatesTargetYaw", a_outComponent.rotatesTargetYaw);
            a_outComponent.followsTarget =
                a_json.value("followsTarget", a_outComponent.followsTarget);
        }

        [[nodiscard]] Json serialize_trigger_volume(
            const ECS::TriggerVolumeComponent& a_component)
        {
            return Json{
                { "includeTriggers", a_component.includeTriggers },
            };
        }

        void deserialize_trigger_volume(
            const Json& a_json,
            ECS::TriggerVolumeComponent& a_outComponent)
        {
            a_outComponent.includeTriggers =
                a_json.value("includeTriggers", a_outComponent.includeTriggers);
            a_outComponent.overlappingEntities.clear();
            a_outComponent.enteredEntities.clear();
            a_outComponent.exitedEntities.clear();
        }

        [[nodiscard]] Json serialize_interactable(
            const ECS::InteractableComponent& a_component)
        {
            return Json{
                { "displayName", a_component.displayName },
                { "maxDistance", a_component.maxDistance },
                { "holdDuration", a_component.holdDuration },
                { "isEnabled", a_component.isEnabled },
            };
        }

        void deserialize_interactable(
            const Json& a_json,
            ECS::InteractableComponent& a_outComponent)
        {
            a_outComponent.displayName =
                a_json.value("displayName", std::string{});
            a_outComponent.maxDistance =
                a_json.value("maxDistance", a_outComponent.maxDistance);
            a_outComponent.holdDuration =
                a_json.value("holdDuration", a_outComponent.holdDuration);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
        }

        [[nodiscard]] const char* to_string(
            ECS::DemoEnemyState a_state) noexcept
        {
            switch (a_state)
            {
            case ECS::DemoEnemyState::Patrol:
                return "Patrol";
            case ECS::DemoEnemyState::MoveToTarget:
                return "MoveToTarget";
            case ECS::DemoEnemyState::ChasePlayer:
                return "ChasePlayer";
            case ECS::DemoEnemyState::Idle:
            default:
                return "Idle";
            }
        }

        [[nodiscard]] ECS::DemoEnemyState parse_demo_enemy_state(
            const Json& a_json) noexcept
        {
            const std::string stateName = a_json.get<std::string>();
            if (stateName == "Patrol")
            {
                return ECS::DemoEnemyState::Patrol;
            }
            if (stateName == "MoveToTarget")
            {
                return ECS::DemoEnemyState::MoveToTarget;
            }
            if (stateName == "ChasePlayer")
            {
                return ECS::DemoEnemyState::ChasePlayer;
            }

            return ECS::DemoEnemyState::Idle;
        }

        [[nodiscard]] Json serialize_demo_enemy(
            const ECS::DemoEnemyComponent& a_component)
        {
            Json patrolPoints = Json::array();
            for (const Math::float3& point : a_component.patrolPoints)
            {
                patrolPoints.push_back(serialize_float3(point));
            }

            return Json{
                { "targetEntity", a_component.targetEntity },
                { "patrolPoints", std::move(patrolPoints) },
                { "requestedDestination",
                    serialize_float3(a_component.requestedDestination) },
                { "state", to_string(a_component.state) },
                { "patrolIndex", a_component.patrolIndex },
                { "chaseDistance", a_component.chaseDistance },
                { "stopDistance", a_component.stopDistance },
                { "hasRequestedDestination",
                    a_component.hasRequestedDestination },
                { "isEnabled", a_component.isEnabled },
            };
        }

        void deserialize_demo_enemy(
            const Json& a_json,
            ECS::DemoEnemyComponent& a_outComponent)
        {
            a_outComponent.targetEntity =
                a_json.value("targetEntity", k_invalidEntityId);
            a_outComponent.patrolPoints.clear();
            const Json patrolPoints = a_json.value("patrolPoints", Json::array());
            if (patrolPoints.is_array())
            {
                for (const Json& pointJson : patrolPoints)
                {
                    Math::float3 point{};
                    deserialize_float3(pointJson, point);
                    a_outComponent.patrolPoints.push_back(point);
                }
            }
            if (const Json::const_iterator destinationIt =
                a_json.find("requestedDestination");
                destinationIt != a_json.end())
            {
                deserialize_float3(
                    *destinationIt, a_outComponent.requestedDestination);
            }
            if (const Json::const_iterator stateIt = a_json.find("state");
                stateIt != a_json.end())
            {
                a_outComponent.state = parse_demo_enemy_state(*stateIt);
            }
            a_outComponent.patrolIndex =
                a_json.value("patrolIndex", a_outComponent.patrolIndex);
            a_outComponent.chaseDistance =
                a_json.value("chaseDistance", a_outComponent.chaseDistance);
            a_outComponent.stopDistance =
                a_json.value("stopDistance", a_outComponent.stopDistance);
            a_outComponent.hasRequestedDestination =
                a_json.value("hasRequestedDestination",
                    a_outComponent.hasRequestedDestination);
            a_outComponent.isEnabled =
                a_json.value("isEnabled", a_outComponent.isEnabled);
        }

        [[nodiscard]] Json serialize_mesh_filter(
            const ECS::MeshFilterComponent& a_component,
            const SceneSerializer::SaveOptions& a_options)
        {
            Json meshFilterJson = {
                { "meshId", a_component.meshId },
            };

            std::string modelName = a_component.modelName;
            if (modelName.empty() && a_options.assetManager != nullptr &&
                a_component.meshId != ECS::k_invalidMeshId)
            {
                (void)a_options.assetManager->get_model_name_from_mesh_id(
                    a_component.meshId,
                    modelName);
            }

            if (!modelName.empty())
            {
                meshFilterJson["modelName"] = modelName;
            }

            return meshFilterJson;
        }

        void deserialize_mesh_filter(
            const Json& a_json,
            const SceneSerializer::LoadOptions& a_options,
            ECS::MeshFilterComponent& a_outComponent)
        {
            a_outComponent.modelName =
                a_json.value("modelName", std::string{});
            a_outComponent.meshId =
                a_json.value("meshId", ECS::k_invalidMeshId);

            if (!a_outComponent.modelName.empty() &&
                a_options.assetManager != nullptr)
            {
                uint32_t meshId = ECS::k_invalidMeshId;
                if (a_options.assetManager->resolve_model_mesh_id(
                    a_outComponent.modelName,
                    meshId))
                {
                    a_outComponent.meshId = meshId;
                    return;
                }
            }

            if (a_outComponent.modelName.empty() &&
                a_options.assetManager != nullptr &&
                a_outComponent.meshId != ECS::k_invalidMeshId)
            {
                (void)a_options.assetManager->get_model_name_from_mesh_id(
                    a_outComponent.meshId,
                    a_outComponent.modelName);
            }
        }

        [[nodiscard]] Json serialize_static_mesh_renderer(
            const ECS::StaticMeshRendererComponent& a_component,
            const SceneSerializer::SaveOptions& a_options)
        {
            Json rendererJson = {
                { "visible", a_component.visible },
                { "castsShadow", a_component.castsShadow },
                { "receivesShadow", a_component.receivesShadow },
                { "renderQueue", to_string(a_component.renderQueue) },
                { "shadowCasterMode", to_string(a_component.shadowCasterMode) },
            };

            if (!a_component.materialHandle.valid())
            {
                return rendererJson;
            }

            if (a_options.assetManager != nullptr)
            {
                std::string materialName{};
                if (a_options.assetManager->get_material_name(
                    a_component.materialHandle,
                    materialName))
                {
                    rendererJson["materialName"] = materialName;
                    return rendererJson;
                }
            }

            rendererJson["materialHandleIndex"] = a_component.materialHandle.index;
            rendererJson["materialHandleGeneration"] =
                a_component.materialHandle.generation;
            return rendererJson;
        }

        void deserialize_static_mesh_renderer(
            const Json& a_json,
            const SceneSerializer::LoadOptions& a_options,
            ECS::StaticMeshRendererComponent& a_outComponent)
        {
            a_outComponent.materialHandle = {};

            const std::string materialName =
                a_json.value("materialName", std::string{});
            if (!materialName.empty() && a_options.assetManager != nullptr)
            {
                MaterialHandle materialHandle{};
                if (a_options.assetManager->get_material(materialName, materialHandle))
                {
                    a_outComponent.materialHandle = materialHandle;
                }
            }

            if (!a_outComponent.materialHandle.valid())
            {
                a_outComponent.materialHandle.index =
                    a_json.value("materialHandleIndex", MaterialHandle::k_invalid);
                a_outComponent.materialHandle.generation =
                    a_json.value("materialHandleGeneration", 0u);
                if (a_json.contains("materialId"))
                {
                    a_outComponent.materialHandle.index =
                        a_json.at("materialId").get<uint32_t>();
                    a_outComponent.materialHandle.generation = 0u;
                }
            }
            a_outComponent.visible =
                a_json.value("visible", a_outComponent.visible);
            a_outComponent.castsShadow =
                a_json.value("castsShadow", a_outComponent.castsShadow);
            a_outComponent.receivesShadow =
                a_json.value("receivesShadow", a_outComponent.receivesShadow);
            a_outComponent.renderQueue = render_queue_from_string(
                a_json.value(
                    "renderQueue",
                    std::string(to_string(a_outComponent.renderQueue))));
            a_outComponent.shadowCasterMode = shadow_caster_mode_from_string(
                a_json.value(
                    "shadowCasterMode",
                    std::string(to_string(a_outComponent.shadowCasterMode))));
        }

        [[nodiscard]] Json serialize_skinned_mesh_renderer(
            const ECS::SkinnedMeshRendererComponent& a_component,
            const SceneSerializer::SaveOptions& a_options)
        {
            ECS::StaticMeshRendererComponent proxy{};
            proxy.materialHandle = a_component.materialHandle;
            proxy.visible = a_component.visible;
            proxy.castsShadow = a_component.castsShadow;
            proxy.receivesShadow = a_component.receivesShadow;
            proxy.renderQueue = a_component.renderQueue;
            proxy.shadowCasterMode = a_component.shadowCasterMode;
            return serialize_static_mesh_renderer(proxy, a_options);
        }

        void deserialize_skinned_mesh_renderer(
            const Json& a_json,
            const SceneSerializer::LoadOptions& a_options,
            ECS::SkinnedMeshRendererComponent& a_outComponent)
        {
            ECS::StaticMeshRendererComponent proxy{};
            deserialize_static_mesh_renderer(a_json, a_options, proxy);
            a_outComponent.materialHandle = proxy.materialHandle;
            a_outComponent.visible = proxy.visible;
            a_outComponent.castsShadow = proxy.castsShadow;
            a_outComponent.receivesShadow = proxy.receivesShadow;
            a_outComponent.renderQueue = proxy.renderQueue;
            a_outComponent.shadowCasterMode = proxy.shadowCasterMode;
        }

        [[nodiscard]] Json serialize_particle_emitter(
            const ECS::ParticleEmitterComponent& a_component,
            const SceneSerializer::SaveOptions& a_options)
        {
            Json emitterJson = {
                { "startColor", serialize_float4(a_component.startColor) },
                { "endColor", serialize_float4(a_component.endColor) },
                { "velocityMin", serialize_float3(a_component.velocityMin) },
                { "velocityMax", serialize_float3(a_component.velocityMax) },
                { "acceleration", serialize_float3(a_component.acceleration) },
                { "startSize", a_component.startSize },
                { "endSize", a_component.endSize },
                { "minLifetime", a_component.minLifetime },
                { "maxLifetime", a_component.maxLifetime },
                { "emitRate", a_component.emitRate },
                { "burstCount", a_component.burstCount },
                { "maxParticleCount", a_component.maxParticleCount },
                { "randomSeed", a_component.randomSeed },
                { "billboardMode", to_string(a_component.billboardMode) },
                { "isPlaying", a_component.isPlaying },
                { "isVisible", a_component.isVisible },
            };

            if (!a_component.materialHandle.valid())
            {
                return emitterJson;
            }

            if (a_options.assetManager != nullptr)
            {
                std::string materialName{};
                if (a_options.assetManager->get_material_name(
                    a_component.materialHandle,
                    materialName))
                {
                    emitterJson["materialName"] = materialName;
                    return emitterJson;
                }
            }

            emitterJson["materialHandleIndex"] = a_component.materialHandle.index;
            emitterJson["materialHandleGeneration"] =
                a_component.materialHandle.generation;
            return emitterJson;
        }

        void deserialize_particle_emitter(
            const Json& a_json,
            const SceneSerializer::LoadOptions& a_options,
            ECS::ParticleEmitterComponent& a_outComponent)
        {
            a_outComponent.materialHandle = {};

            const std::string materialName =
                a_json.value("materialName", std::string{});
            if (!materialName.empty() && a_options.assetManager != nullptr)
            {
                MaterialHandle materialHandle{};
                if (a_options.assetManager->get_material(materialName, materialHandle))
                {
                    a_outComponent.materialHandle = materialHandle;
                }
            }

            if (!a_outComponent.materialHandle.valid())
            {
                a_outComponent.materialHandle.index =
                    a_json.value("materialHandleIndex", MaterialHandle::k_invalid);
                a_outComponent.materialHandle.generation =
                    a_json.value("materialHandleGeneration", 0u);
            }

            if (const Json::const_iterator colorIt =
                a_json.find("startColor");
                colorIt != a_json.end())
            {
                deserialize_float4(*colorIt, a_outComponent.startColor);
            }
            if (const Json::const_iterator colorIt = a_json.find("endColor");
                colorIt != a_json.end())
            {
                deserialize_float4(*colorIt, a_outComponent.endColor);
            }
            if (const Json::const_iterator velocityIt =
                a_json.find("velocityMin");
                velocityIt != a_json.end())
            {
                deserialize_float3(*velocityIt, a_outComponent.velocityMin);
            }
            if (const Json::const_iterator velocityIt =
                a_json.find("velocityMax");
                velocityIt != a_json.end())
            {
                deserialize_float3(*velocityIt, a_outComponent.velocityMax);
            }
            if (const Json::const_iterator accelerationIt =
                a_json.find("acceleration");
                accelerationIt != a_json.end())
            {
                deserialize_float3(*accelerationIt, a_outComponent.acceleration);
            }

            a_outComponent.startSize = (std::max)(
                a_json.value("startSize", a_outComponent.startSize),
                0.0f);
            a_outComponent.endSize = (std::max)(
                a_json.value("endSize", a_outComponent.endSize),
                0.0f);
            a_outComponent.minLifetime = (std::max)(
                a_json.value("minLifetime", a_outComponent.minLifetime),
                0.01f);
            a_outComponent.maxLifetime = (std::max)(
                a_json.value("maxLifetime", a_outComponent.maxLifetime),
                a_outComponent.minLifetime);
            a_outComponent.emitRate = (std::max)(
                a_json.value("emitRate", a_outComponent.emitRate),
                0.0f);
            a_outComponent.burstCount =
                a_json.value("burstCount", a_outComponent.burstCount);
            a_outComponent.maxParticleCount = (std::clamp)(
                a_json.value(
                    "maxParticleCount", a_outComponent.maxParticleCount),
                1u,
                GpuData::k_maxParticleCount);
            a_outComponent.randomSeed =
                a_json.value("randomSeed", a_outComponent.randomSeed);
            if (const Json::const_iterator modeIt =
                a_json.find("billboardMode");
                modeIt != a_json.end())
            {
                a_outComponent.billboardMode =
                    parse_particle_billboard_mode(*modeIt);
            }
            a_outComponent.isPlaying =
                a_json.value("isPlaying", a_outComponent.isPlaying);
            a_outComponent.isVisible =
                a_json.value("isVisible", a_outComponent.isVisible);

            ECS::ParticleEmitterComponent defaultComponent{};
            a_outComponent.runtimeParticleBase =
                defaultComponent.runtimeParticleBase;
            a_outComponent.runtimeParticleCapacity = 0;
            a_outComponent.runtimeSpawnCursor = 0;
            a_outComponent.runtimeEmitAccumulator = 0.0f;
        }

        [[nodiscard]] Json serialize_animation(
            const ECS::AnimationComponent& a_component)
        {
            return Json{
                { "animationIndex", a_component.animationIndex },
                { "frame", a_component.frame },
                { "time", a_component.time },
                { "speed", a_component.speed },
                { "isPlaying", a_component.isPlaying },
                { "loops", a_component.loops },
            };
        }

        void deserialize_animation(
            const Json& a_json,
            ECS::AnimationComponent& a_outComponent)
        {
            a_outComponent.animationIndex =
                a_json.value("animationIndex", a_outComponent.animationIndex);
            a_outComponent.frame = a_json.value("frame", a_outComponent.frame);
            a_outComponent.time = a_json.value("time", a_outComponent.time);
            a_outComponent.speed = a_json.value("speed", a_outComponent.speed);
            a_outComponent.isPlaying =
                a_json.value("isPlaying", a_outComponent.isPlaying);
            a_outComponent.loops = a_json.value("loops", a_outComponent.loops);
        }

        [[nodiscard]] Json serialize_audio_source(
            const ECS::AudioSourceComponent& a_component)
        {
            return Json{
                { "fileName", a_component.fileName },
                { "encoding", a_component.encoding == ECS::AudioEncoding::Adpcm
                    ? "ADPCM"
                    : "PCM" },
                { "loop", a_component.loop },
                { "playOnStart", a_component.playOnStart },
                { "spatialBlend", a_component.spatialBlend },
                { "volume", a_component.volume },
            };
        }

        void deserialize_audio_source(
            const Json& a_json, ECS::AudioSourceComponent& a_outComponent)
        {
            a_outComponent.fileName =
                a_json.value("fileName", std::string{});
            const std::string encoding = a_json.value(
                "encoding",
                std::string("PCM"));
            a_outComponent.encoding =
                encoding == "ADPCM"
                    ? ECS::AudioEncoding::Adpcm
                    : ECS::AudioEncoding::Pcm;
            a_outComponent.loop = a_json.value("loop", false);
            a_outComponent.playOnStart =
                a_json.value("playOnStart", false);
            a_outComponent.spatialBlend =
                a_json.value("spatialBlend", 0.0f);
            a_outComponent.volume = a_json.value("volume", 1.0f);
            a_outComponent.sourceHandle = {};
            a_outComponent.isPlaying = false;
            a_outComponent.playRequested = false;
            a_outComponent.stopRequested = false;
            a_outComponent.hasStarted = false;
        }

        [[nodiscard]] Json serialize_rigid_body(
            const ECS::RigidBodyComponent& a_component)
        {
            return Json{
                { "motion", to_string(a_component.motion) },
                { "linearVelocity", serialize_float3(a_component.linearVelocity) },
                { "angularVelocity", serialize_float3(a_component.angularVelocity) },
                { "mass", a_component.mass },
                { "linearDamping", a_component.linearDamping },
                { "angularDamping", a_component.angularDamping },
                { "useGravity", a_component.useGravity },
            };
        }

        void deserialize_rigid_body(
            const Json& a_json, ECS::RigidBodyComponent& a_outComponent)
        {
            a_outComponent.body = {};
            a_outComponent.motion = parse_motion_type(
                a_json.value("motion", std::string("Dynamic")));
            if (const Json::const_iterator linearVelocityIt =
                a_json.find("linearVelocity");
                linearVelocityIt != a_json.end())
            {
                deserialize_float3(*linearVelocityIt,
                    a_outComponent.linearVelocity);
            }
            if (const Json::const_iterator angularVelocityIt =
                a_json.find("angularVelocity");
                angularVelocityIt != a_json.end())
            {
                deserialize_float3(*angularVelocityIt,
                    a_outComponent.angularVelocity);
            }
            a_outComponent.mass = a_json.value("mass", 1.0f);
            a_outComponent.linearDamping =
                a_json.value("linearDamping", 0.05f);
            a_outComponent.angularDamping =
                a_json.value("angularDamping", 0.05f);
            a_outComponent.useGravity = a_json.value("useGravity", true);
            a_outComponent.isCreated = false;
        }

        [[nodiscard]] Json serialize_collider(
            const ECS::ColliderComponent& a_component)
        {
            return Json{
                { "type", to_string(a_component.type) },
                { "meshModelName", a_component.meshModelName },
                { "offset", serialize_float3(a_component.offset) },
                { "halfExtent", serialize_float3(a_component.halfExtent) },
                { "radius", a_component.radius },
                { "halfHeight", a_component.halfHeight },
                { "friction", a_component.friction },
                { "restitution", a_component.restitution },
                { "layer", a_component.layer },
                { "mask", a_component.mask },
                { "isTrigger", a_component.isTrigger },
            };
        }

        void deserialize_collider(
            const Json& a_json, ECS::ColliderComponent& a_outComponent)
        {
            a_outComponent.type = parse_shape_type(
                a_json.value("type", std::string("Box")));
            a_outComponent.meshModelName =
                a_json.value("meshModelName", std::string{});
            if (const Json::const_iterator offsetIt = a_json.find("offset");
                offsetIt != a_json.end())
            {
                deserialize_float3(*offsetIt, a_outComponent.offset);
            }
            if (const Json::const_iterator halfExtentIt =
                a_json.find("halfExtent");
                halfExtentIt != a_json.end())
            {
                deserialize_float3(*halfExtentIt, a_outComponent.halfExtent);
            }
            a_outComponent.radius = a_json.value("radius", 0.5f);
            a_outComponent.halfHeight = a_json.value("halfHeight", 0.5f);
            a_outComponent.friction = a_json.value("friction", 0.2f);
            a_outComponent.restitution = a_json.value("restitution", 0.0f);
            a_outComponent.layer = a_json.value("layer", uint16_t{ 0 });
            a_outComponent.mask = a_json.value("mask", uint16_t{ 0xFFFFu });
            a_outComponent.isTrigger = a_json.value("isTrigger", false);
        }

        [[nodiscard]] Json serialize_character_controller(
            const ECS::CharacterControllerComponent& a_component)
        {
            return Json{
                { "moveVelocity", serialize_float3(a_component.moveVelocity) },
                { "verticalVelocity", a_component.verticalVelocity },
                { "maxSpeed", a_component.maxSpeed },
                { "gravity", a_component.gravity },
                { "jumpSpeed", a_component.jumpSpeed },
                { "groundCheckDistance", a_component.groundCheckDistance },
                { "skinWidth", a_component.skinWidth },
            };
        }

        void deserialize_character_controller(
            const Json& a_json,
            ECS::CharacterControllerComponent& a_outComponent)
        {
            if (const Json::const_iterator moveVelocityIt =
                a_json.find("moveVelocity");
                moveVelocityIt != a_json.end())
            {
                deserialize_float3(
                    *moveVelocityIt, a_outComponent.moveVelocity);
            }
            a_outComponent.verticalVelocity =
                a_json.value("verticalVelocity", 0.0f);
            a_outComponent.maxSpeed = a_json.value("maxSpeed", 6.0f);
            a_outComponent.gravity = a_json.value("gravity", 9.80665f);
            a_outComponent.jumpSpeed = a_json.value("jumpSpeed", 5.0f);
            a_outComponent.groundCheckDistance =
                a_json.value("groundCheckDistance", 0.12f);
            a_outComponent.skinWidth = a_json.value("skinWidth", 0.03f);
            a_outComponent.isGrounded = false;
            a_outComponent.jumpRequested = false;
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
            case ECS::ScriptFieldType::EntityRef:
                return "EntityRef";
            case ECS::ScriptFieldType::ClassRef:
                return "ClassRef";
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
            if (typeName == "EntityRef")
            {
                return ECS::ScriptFieldType::EntityRef;
            }
            if (typeName == "ClassRef")
            {
                return ECS::ScriptFieldType::ClassRef;
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
                { "entityValue", a_fieldValue.entityValue },
                { "classValue", a_fieldValue.classValue },
                { "groupName", a_fieldValue.groupName },
                { "referenceRole", static_cast<uint32_t>(a_fieldValue.referenceRole) },
                { "flags", static_cast<uint32_t>(a_fieldValue.flags) },
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
            a_outFieldValue.entityValue =
                a_json.value("entityValue", k_invalidEntityId);
            a_outFieldValue.classValue = a_json.value("classValue", std::string{});
            a_outFieldValue.groupName = a_json.value("groupName", std::string{});
            a_outFieldValue.referenceRole =
                static_cast<ECS::ScriptFieldReferenceRole>(
                    a_json.value("referenceRole", 0u));
            a_outFieldValue.flags =
                static_cast<ECS::ScriptFieldFlags>(
                    a_json.value(
                        "flags",
                        static_cast<uint32_t>(
                            ECS::ScriptFieldFlags::EditAnywhere |
                            ECS::ScriptFieldFlags::Serialize)));
        }

        [[nodiscard]] Json serialize_script(
            const ECS::ScriptComponent& a_component,
            const SceneSerializer::SaveOptions& a_options)
        {
            Json fieldValues = Json::array();
            std::vector<ECS::ScriptFieldValue> allFieldValues =
                a_component.serializedFieldValues;
            allFieldValues.reserve(
                a_component.serializedFieldValues.size() +
                a_component.transientFieldValues.size());
            for (const ECS::ScriptFieldValue& fieldValue :
                a_component.transientFieldValues)
            {
                const auto existingIt =
                    std::find_if(allFieldValues.begin(), allFieldValues.end(),
                        [&fieldValue](const ECS::ScriptFieldValue& a_existingFieldValue)
                        {
                            return a_existingFieldValue.name == fieldValue.name;
                        });
                if (existingIt != allFieldValues.end())
                {
                    continue;
                }

                allFieldValues.push_back(fieldValue);
            }

            for (const ECS::ScriptFieldValue& fieldValue : allFieldValues)
            {
                if (a_options.shouldSerializeScriptField != nullptr &&
                    !a_options.shouldSerializeScriptField(
                        a_component.className,
                        fieldValue.name,
                        a_options.userData))
                {
                    continue;
                }

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
            a_outComponent.serializedFieldValues.clear();
            a_outComponent.transientFieldValues.clear();

            const Json fieldValuesJson =
                a_json.value("fieldValues", Json::array());
            if (!fieldValuesJson.is_array())
            {
                return;
            }

            a_outComponent.serializedFieldValues.reserve(fieldValuesJson.size());
            for (const Json& fieldValueJson : fieldValuesJson)
            {
                ECS::ScriptFieldValue fieldValue{};
                deserialize_script_field_value(fieldValueJson, fieldValue);
                a_outComponent.serializedFieldValues.push_back(std::move(fieldValue));
            }
        }

        [[nodiscard]] Json serialize_object_definition(
            const ObjectDefinition& a_definition,
            const SceneSerializer::SaveOptions& a_options)
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

            if (const ECS::CanvasComponent* canvas =
                a_definition.prototype.get_component_ptr<ECS::CanvasComponent>();
                canvas != nullptr)
            {
                componentsJson["canvas"] = serialize_canvas(*canvas);
            }

            if (const ECS::UiRectTransformComponent* rect =
                a_definition.prototype.get_component_ptr<
                    ECS::UiRectTransformComponent>();
                rect != nullptr)
            {
                componentsJson["uiRectTransform"] =
                    serialize_ui_rect_transform(*rect);
            }

            if (const ECS::UiLayoutGroupComponent* layout =
                a_definition.prototype.get_component_ptr<
                    ECS::UiLayoutGroupComponent>();
                layout != nullptr)
            {
                componentsJson["uiLayoutGroup"] =
                    serialize_ui_layout_group(*layout);
            }

            if (const ECS::TextRendererComponent* text =
                a_definition.prototype.get_component_ptr<
                    ECS::TextRendererComponent>();
                text != nullptr)
            {
                componentsJson["textRenderer"] = serialize_text_renderer(*text);
            }

            if (const ECS::DirectionalLightComponent* directionalLight =
                a_definition.prototype.get_component_ptr<
                    ECS::DirectionalLightComponent>();
                directionalLight != nullptr)
            {
                componentsJson["directionalLight"] =
                    serialize_directional_light(*directionalLight);
            }

            if (const ECS::PointLightComponent* pointLight =
                a_definition.prototype.get_component_ptr<ECS::PointLightComponent>();
                pointLight != nullptr)
            {
                componentsJson["pointLight"] = serialize_point_light(*pointLight);
            }

            if (const ECS::SpotLightComponent* spotLight =
                a_definition.prototype.get_component_ptr<ECS::SpotLightComponent>();
                spotLight != nullptr)
            {
                componentsJson["spotLight"] = serialize_spot_light(*spotLight);
            }

            if (const ECS::FirstPersonCameraControllerComponent* controller =
                a_definition.prototype.get_component_ptr<
                    ECS::FirstPersonCameraControllerComponent>();
                controller != nullptr)
            {
                componentsJson["firstPersonCameraController"] =
                    serialize_first_person_camera_controller(*controller);
            }

            if (const ECS::MeshFilterComponent* meshFilter =
                a_definition.prototype.get_component_ptr<ECS::MeshFilterComponent>();
                meshFilter != nullptr)
            {
                componentsJson["meshFilter"] =
                    serialize_mesh_filter(*meshFilter, a_options);
            }

            if (const ECS::NavAgentComponent* navAgent =
                a_definition.prototype.get_component_ptr<ECS::NavAgentComponent>();
                navAgent != nullptr)
            {
                componentsJson["navAgent"] = serialize_nav_agent(*navAgent);
            }

            if (const ECS::DemoEnemyComponent* demoEnemy =
                a_definition.prototype.get_component_ptr<ECS::DemoEnemyComponent>();
                demoEnemy != nullptr)
            {
                componentsJson["demoEnemy"] = serialize_demo_enemy(*demoEnemy);
            }

            if (const ECS::NavMeshBakeSourceComponent* navMeshBakeSource =
                a_definition.prototype.get_component_ptr<ECS::NavMeshBakeSourceComponent>();
                navMeshBakeSource != nullptr)
            {
                componentsJson["navMeshBakeSource"] =
                    serialize_nav_mesh_bake_source(*navMeshBakeSource);
            }

            if (const ECS::StaticMeshRendererComponent* renderer =
                a_definition.prototype.get_component_ptr<ECS::StaticMeshRendererComponent>();
                renderer != nullptr)
            {
                componentsJson["staticMeshRenderer"] =
                    serialize_static_mesh_renderer(*renderer, a_options);
            }

            if (const ECS::SkinnedMeshRendererComponent* renderer =
                a_definition.prototype.get_component_ptr<
                    ECS::SkinnedMeshRendererComponent>();
                renderer != nullptr)
            {
                componentsJson["skinnedMeshRenderer"] =
                    serialize_skinned_mesh_renderer(*renderer, a_options);
            }

            if (const ECS::AnimationComponent* animation =
                a_definition.prototype.get_component_ptr<ECS::AnimationComponent>();
                animation != nullptr)
            {
                componentsJson["animation"] = serialize_animation(*animation);
            }

            if (const ECS::ParticleEmitterComponent* emitter =
                a_definition.prototype.get_component_ptr<
                    ECS::ParticleEmitterComponent>();
                emitter != nullptr)
            {
                componentsJson["particleEmitter"] =
                    serialize_particle_emitter(*emitter, a_options);
            }

            if (const ECS::AudioSourceComponent* audioSource =
                a_definition.prototype.get_component_ptr<ECS::AudioSourceComponent>();
                audioSource != nullptr)
            {
                componentsJson["audioSource"] =
                    serialize_audio_source(*audioSource);
            }

            if (const ECS::RigidBodyComponent* rigidBody =
                a_definition.prototype.get_component_ptr<ECS::RigidBodyComponent>();
                rigidBody != nullptr)
            {
                componentsJson["rigidBody"] =
                    serialize_rigid_body(*rigidBody);
            }

            if (const ECS::ColliderComponent* collider =
                a_definition.prototype.get_component_ptr<ECS::ColliderComponent>();
                collider != nullptr)
            {
                componentsJson["collider"] = serialize_collider(*collider);
            }

            if (const ECS::TriggerVolumeComponent* trigger =
                a_definition.prototype.get_component_ptr<ECS::TriggerVolumeComponent>();
                trigger != nullptr)
            {
                componentsJson["triggerVolume"] = serialize_trigger_volume(*trigger);
            }

            if (const ECS::InteractableComponent* interactable =
                a_definition.prototype.get_component_ptr<ECS::InteractableComponent>();
                interactable != nullptr)
            {
                componentsJson["interactable"] = serialize_interactable(*interactable);
            }

            if (const ECS::CharacterControllerComponent* characterController =
                a_definition.prototype.get_component_ptr<
                    ECS::CharacterControllerComponent>();
                characterController != nullptr)
            {
                componentsJson["characterController"] =
                    serialize_character_controller(*characterController);
            }

            if (const ECS::ScriptComponent* script =
                a_definition.prototype.get_component_ptr<ECS::ScriptComponent>();
                script != nullptr)
            {
                componentsJson["script"] = serialize_script(*script, a_options);
            }

            objectJson["components"] = std::move(componentsJson);
            return objectJson;
        }

        [[nodiscard]] Result deserialize_object_definition(
            const Json& a_json,
            const SceneSerializer::LoadOptions& a_options,
            ObjectDefinition& a_outDefinition) noexcept
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

                if (const Json::const_iterator canvasIt =
                    componentsJson.find("canvas");
                    canvasIt != componentsJson.end())
                {
                    ECS::CanvasComponent canvas{};
                    deserialize_canvas(*canvasIt, canvas);
                    objectDefinition.prototype.add_component(canvas);
                }

                if (const Json::const_iterator rectIt =
                    componentsJson.find("uiRectTransform");
                    rectIt != componentsJson.end())
                {
                    ECS::UiRectTransformComponent rect{};
                    deserialize_ui_rect_transform(*rectIt, rect);
                    objectDefinition.prototype.add_component(rect);
                }

                if (const Json::const_iterator layoutIt =
                    componentsJson.find("uiLayoutGroup");
                    layoutIt != componentsJson.end())
                {
                    ECS::UiLayoutGroupComponent layout{};
                    deserialize_ui_layout_group(*layoutIt, layout);
                    objectDefinition.prototype.add_component(layout);
                }

                if (const Json::const_iterator textIt =
                    componentsJson.find("textRenderer");
                    textIt != componentsJson.end())
                {
                    ECS::TextRendererComponent text{};
                    deserialize_text_renderer(*textIt, text);
                    objectDefinition.prototype.add_component(text);
                }

                if (const Json::const_iterator directionalLightIt =
                    componentsJson.find("directionalLight");
                    directionalLightIt != componentsJson.end())
                {
                    ECS::DirectionalLightComponent directionalLight{};
                    deserialize_directional_light(
                        *directionalLightIt,
                        directionalLight);
                    objectDefinition.prototype.add_component(directionalLight);
                }

                if (const Json::const_iterator pointLightIt =
                    componentsJson.find("pointLight");
                    pointLightIt != componentsJson.end())
                {
                    ECS::PointLightComponent pointLight{};
                    deserialize_point_light(*pointLightIt, pointLight);
                    objectDefinition.prototype.add_component(pointLight);
                }

                if (const Json::const_iterator spotLightIt =
                    componentsJson.find("spotLight");
                    spotLightIt != componentsJson.end())
                {
                    ECS::SpotLightComponent spotLight{};
                    deserialize_spot_light(*spotLightIt, spotLight);
                    objectDefinition.prototype.add_component(spotLight);
                }

                if (const Json::const_iterator controllerIt =
                    componentsJson.find("firstPersonCameraController");
                    controllerIt != componentsJson.end())
                {
                    ECS::FirstPersonCameraControllerComponent controller{};
                    deserialize_first_person_camera_controller(
                        *controllerIt, controller);
                    objectDefinition.prototype.add_component(controller);
                }

                if (const Json::const_iterator meshFilterIt =
                    componentsJson.find("meshFilter");
                    meshFilterIt != componentsJson.end())
                {
                    ECS::MeshFilterComponent meshFilter{};
                    deserialize_mesh_filter(*meshFilterIt, a_options, meshFilter);
                    objectDefinition.prototype.add_component(meshFilter);
                }

                if (const Json::const_iterator navAgentIt =
                    componentsJson.find("navAgent");
                    navAgentIt != componentsJson.end())
                {
                    ECS::NavAgentComponent navAgent{};
                    deserialize_nav_agent(*navAgentIt, navAgent);
                    objectDefinition.prototype.add_component(navAgent);
                }

                if (const Json::const_iterator demoEnemyIt =
                    componentsJson.find("demoEnemy");
                    demoEnemyIt != componentsJson.end())
                {
                    ECS::DemoEnemyComponent demoEnemy{};
                    deserialize_demo_enemy(*demoEnemyIt, demoEnemy);
                    objectDefinition.prototype.add_component(demoEnemy);
                }

                if (const Json::const_iterator navMeshBakeSourceIt =
                    componentsJson.find("navMeshBakeSource");
                    navMeshBakeSourceIt != componentsJson.end())
                {
                    ECS::NavMeshBakeSourceComponent navMeshBakeSource{};
                    deserialize_nav_mesh_bake_source(
                        *navMeshBakeSourceIt, navMeshBakeSource);
                    objectDefinition.prototype.add_component(navMeshBakeSource);
                }

                if (const Json::const_iterator rendererIt =
                    componentsJson.find("staticMeshRenderer");
                    rendererIt != componentsJson.end())
                {
                    ECS::StaticMeshRendererComponent renderer{};
                    deserialize_static_mesh_renderer(*rendererIt, a_options, renderer);
                    objectDefinition.prototype.add_component(renderer);
                }

                if (const Json::const_iterator rendererIt =
                    componentsJson.find("skinnedMeshRenderer");
                    rendererIt != componentsJson.end())
                {
                    ECS::SkinnedMeshRendererComponent renderer{};
                    deserialize_skinned_mesh_renderer(
                        *rendererIt, a_options, renderer);
                    objectDefinition.prototype.add_component(renderer);
                }

                if (const Json::const_iterator animationIt =
                    componentsJson.find("animation");
                    animationIt != componentsJson.end())
                {
                    ECS::AnimationComponent animation{};
                    deserialize_animation(*animationIt, animation);
                    objectDefinition.prototype.add_component(animation);
                }

                if (const Json::const_iterator emitterIt =
                    componentsJson.find("particleEmitter");
                    emitterIt != componentsJson.end())
                {
                    ECS::ParticleEmitterComponent emitter{};
                    deserialize_particle_emitter(*emitterIt, a_options, emitter);
                    objectDefinition.prototype.add_component(emitter);
                }

                if (const Json::const_iterator audioSourceIt =
                    componentsJson.find("audioSource");
                    audioSourceIt != componentsJson.end())
                {
                    ECS::AudioSourceComponent audioSource{};
                    deserialize_audio_source(*audioSourceIt, audioSource);
                    objectDefinition.prototype.add_component(audioSource);
                }

                if (const Json::const_iterator rigidBodyIt =
                    componentsJson.find("rigidBody");
                    rigidBodyIt != componentsJson.end())
                {
                    ECS::RigidBodyComponent rigidBody{};
                    deserialize_rigid_body(*rigidBodyIt, rigidBody);
                    objectDefinition.prototype.add_component(rigidBody);
                }

                if (const Json::const_iterator colliderIt =
                    componentsJson.find("collider");
                    colliderIt != componentsJson.end())
                {
                    ECS::ColliderComponent collider{};
                    deserialize_collider(*colliderIt, collider);
                    objectDefinition.prototype.add_component(collider);
                }

                if (const Json::const_iterator triggerIt =
                    componentsJson.find("triggerVolume");
                    triggerIt != componentsJson.end())
                {
                    ECS::TriggerVolumeComponent trigger{};
                    deserialize_trigger_volume(*triggerIt, trigger);
                    objectDefinition.prototype.add_component(trigger);
                }

                if (const Json::const_iterator interactableIt =
                    componentsJson.find("interactable");
                    interactableIt != componentsJson.end())
                {
                    ECS::InteractableComponent interactable{};
                    deserialize_interactable(*interactableIt, interactable);
                    objectDefinition.prototype.add_component(interactable);
                }

                if (const Json::const_iterator characterControllerIt =
                    componentsJson.find("characterController");
                    characterControllerIt != componentsJson.end())
                {
                    ECS::CharacterControllerComponent characterController{};
                    deserialize_character_controller(
                        *characterControllerIt, characterController);
                    objectDefinition.prototype.add_component(characterController);
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
        const Core::IO::Path& a_filePath,
        const SaveOptions& a_options) noexcept
    {
        try
        {
            Json root = {
                { "version", k_currentVersion },
                { "name", a_sceneAsset.name() },
                { "objects", Json::array() },
            };
            if (!a_sceneAsset.navigation_mesh_path().empty())
            {
                root["navigationMesh"] = a_sceneAsset.navigation_mesh_path();
            }

            for (const ObjectDefinition& object : a_sceneAsset.objects())
            {
                root["objects"].push_back(serialize_object_definition(
                    object, a_options));
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
        SceneAsset& a_outSceneAsset,
        const LoadOptions& a_options) noexcept
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
            if (version != 1 && version != k_currentVersion)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Scene asset version is not supported.");
            }

            SceneAsset sceneAsset(root.at("name").get<std::string>());
            sceneAsset.set_navigation_mesh_path(
                root.value("navigationMesh", std::string{}));
            const Json& objectsJson = root.at("objects");
            if (!objectsJson.is_array())
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Scene asset objects must be an array.");
            }

            for (const Json& objectJson : objectsJson)
            {
                ObjectDefinition objectDefinition{};
                result = deserialize_object_definition(
                    objectJson, a_options, objectDefinition);
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
