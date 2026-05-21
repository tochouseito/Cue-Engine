#include "ScriptRuntime.h"

// === Core includes ===
#include <IO/Logger.h>

// === PAL includes ===
#include <PAL.h>

// === Engine includes ===
#include "../GameCore/Components.h"
#include "../GameCore/GameObject.h"
#include "../GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace Cue
{
    namespace
    {
        class RuntimeMarionnetteComponent final : public MarionnetteComponent
        {
        public:
            [[nodiscard]] const MarionnetteClass* get_class() const noexcept override
            {
                return m_runtimeClass != nullptr
                    ? m_runtimeClass
                    : MarionnetteComponent::static_class();
            }

            void set_runtime_class(const MarionnetteClass* a_runtimeClass) noexcept
            {
                m_runtimeClass = a_runtimeClass;
            }

        private:
            const MarionnetteClass* m_runtimeClass = nullptr;
        };

        [[nodiscard]] std::string_view to_string_view(CueStringView a_value) noexcept
        {
            return a_value.data != nullptr
                ? std::string_view(a_value.data, a_value.size)
                : std::string_view{};
        }

        [[nodiscard]] CueStringView make_string_view(std::string_view a_value) noexcept
        {
            return CueStringView{
                a_value.data(),
                static_cast<uint32_t>(a_value.size())
            };
        }

        [[nodiscard]] bool has_text(CueStringView a_value) noexcept
        {
            return a_value.data != nullptr && a_value.size > 0u;
        }

        [[nodiscard]] Math::float3 to_math_float3(const CueFloat3& a_value) noexcept
        {
            return Math::float3(a_value.x, a_value.y, a_value.z);
        }

        [[nodiscard]] Math::float2 to_math_float2(const CueFloat2& a_value) noexcept
        {
            return Math::float2(a_value.x, a_value.y);
        }

        [[nodiscard]] float length_squared(const Math::float3& a_value) noexcept
        {
            return a_value.x * a_value.x + a_value.y * a_value.y +
                a_value.z * a_value.z;
        }

        [[nodiscard]] bool to_physics_shape_type(
            CueColliderShapeType a_shapeType,
            Physics::ShapeType& a_outShapeType) noexcept
        {
            switch (a_shapeType)
            {
            case CueColliderShapeType_Box:
                a_outShapeType = Physics::ShapeType::Box;
                return true;
            case CueColliderShapeType_Sphere:
                a_outShapeType = Physics::ShapeType::Sphere;
                return true;
            case CueColliderShapeType_Capsule:
                a_outShapeType = Physics::ShapeType::Capsule;
                return true;
            case CueColliderShapeType_Mesh:
                a_outShapeType = Physics::ShapeType::Mesh;
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] float collider_bounding_radius(
            const ECS::ColliderComponent& a_collider) noexcept
        {
            switch (a_collider.type)
            {
            case Physics::ShapeType::Sphere:
                return a_collider.radius;
            case Physics::ShapeType::Capsule:
            {
                const float verticalRadius =
                    a_collider.halfHeight + a_collider.radius;
                return std::sqrt(
                    a_collider.radius * a_collider.radius +
                    verticalRadius * verticalRadius);
            }
            case Physics::ShapeType::Box:
            case Physics::ShapeType::Mesh:
            default:
                return std::sqrt(length_squared(a_collider.halfExtent));
            }
        }

        [[nodiscard]] PAL::Key to_pal_key(CueKey a_key) noexcept
        {
            switch (a_key)
            {
            case CueKey_Escape: return PAL::Key::Escape;
            case CueKey_Tab: return PAL::Key::Tab;
            case CueKey_CapsLock: return PAL::Key::CapsLock;
            case CueKey_LeftShift: return PAL::Key::LeftShift;
            case CueKey_RightShift: return PAL::Key::RightShift;
            case CueKey_LeftControl: return PAL::Key::LeftControl;
            case CueKey_RightControl: return PAL::Key::RightControl;
            case CueKey_LeftAlt: return PAL::Key::LeftAlt;
            case CueKey_RightAlt: return PAL::Key::RightAlt;
            case CueKey_Space: return PAL::Key::Space;
            case CueKey_Enter: return PAL::Key::Enter;
            case CueKey_Backspace: return PAL::Key::Backspace;
            case CueKey_Insert: return PAL::Key::Insert;
            case CueKey_Delete: return PAL::Key::Delete;
            case CueKey_Home: return PAL::Key::Home;
            case CueKey_End: return PAL::Key::End;
            case CueKey_PageUp: return PAL::Key::PageUp;
            case CueKey_PageDown: return PAL::Key::PageDown;
            case CueKey_Left: return PAL::Key::Left;
            case CueKey_Right: return PAL::Key::Right;
            case CueKey_Up: return PAL::Key::Up;
            case CueKey_Down: return PAL::Key::Down;
            case CueKey_Num0: return PAL::Key::Num0;
            case CueKey_Num1: return PAL::Key::Num1;
            case CueKey_Num2: return PAL::Key::Num2;
            case CueKey_Num3: return PAL::Key::Num3;
            case CueKey_Num4: return PAL::Key::Num4;
            case CueKey_Num5: return PAL::Key::Num5;
            case CueKey_Num6: return PAL::Key::Num6;
            case CueKey_Num7: return PAL::Key::Num7;
            case CueKey_Num8: return PAL::Key::Num8;
            case CueKey_Num9: return PAL::Key::Num9;
            case CueKey_A: return PAL::Key::A;
            case CueKey_B: return PAL::Key::B;
            case CueKey_C: return PAL::Key::C;
            case CueKey_D: return PAL::Key::D;
            case CueKey_E: return PAL::Key::E;
            case CueKey_F: return PAL::Key::F;
            case CueKey_G: return PAL::Key::G;
            case CueKey_H: return PAL::Key::H;
            case CueKey_I: return PAL::Key::I;
            case CueKey_J: return PAL::Key::J;
            case CueKey_K: return PAL::Key::K;
            case CueKey_L: return PAL::Key::L;
            case CueKey_M: return PAL::Key::M;
            case CueKey_N: return PAL::Key::N;
            case CueKey_O: return PAL::Key::O;
            case CueKey_P: return PAL::Key::P;
            case CueKey_Q: return PAL::Key::Q;
            case CueKey_R: return PAL::Key::R;
            case CueKey_S: return PAL::Key::S;
            case CueKey_T: return PAL::Key::T;
            case CueKey_U: return PAL::Key::U;
            case CueKey_V: return PAL::Key::V;
            case CueKey_W: return PAL::Key::W;
            case CueKey_X: return PAL::Key::X;
            case CueKey_Y: return PAL::Key::Y;
            case CueKey_Z: return PAL::Key::Z;
            case CueKey_F1: return PAL::Key::F1;
            case CueKey_F2: return PAL::Key::F2;
            case CueKey_F3: return PAL::Key::F3;
            case CueKey_F4: return PAL::Key::F4;
            case CueKey_F5: return PAL::Key::F5;
            case CueKey_F6: return PAL::Key::F6;
            case CueKey_F7: return PAL::Key::F7;
            case CueKey_F8: return PAL::Key::F8;
            case CueKey_F9: return PAL::Key::F9;
            case CueKey_F10: return PAL::Key::F10;
            case CueKey_F11: return PAL::Key::F11;
            case CueKey_F12: return PAL::Key::F12;
            case CueKey_Unknown:
            case CueKey_Count:
            default:
                return PAL::Key::Unknown;
            }
        }

        [[nodiscard]] PAL::MouseButton to_pal_mouse_button(
            CueMouseButton a_button) noexcept
        {
            switch (a_button)
            {
            case CueMouseButton_Left: return PAL::MouseButton::Left;
            case CueMouseButton_Right: return PAL::MouseButton::Right;
            case CueMouseButton_Middle: return PAL::MouseButton::Middle;
            case CueMouseButton_X1: return PAL::MouseButton::X1;
            case CueMouseButton_X2: return PAL::MouseButton::X2;
            case CueMouseButton_Count:
            default:
                return PAL::MouseButton::Count;
            }
        }

        [[nodiscard]] ECS::ScriptFieldType to_script_field_type(
            CueScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case CueScriptFieldType_Int32:
                return ECS::ScriptFieldType::Int32;
            case CueScriptFieldType_Bool:
                return ECS::ScriptFieldType::Bool;
            case CueScriptFieldType_EntityRef:
                return ECS::ScriptFieldType::EntityRef;
            case CueScriptFieldType_ClassRef:
                return ECS::ScriptFieldType::ClassRef;
            case CueScriptFieldType_Float:
            default:
                return ECS::ScriptFieldType::Float;
            }
        }

        [[nodiscard]] CueScriptFieldType to_cue_script_field_type(
            ECS::ScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case ECS::ScriptFieldType::Int32:
                return CueScriptFieldType_Int32;
            case ECS::ScriptFieldType::Bool:
                return CueScriptFieldType_Bool;
            case ECS::ScriptFieldType::EntityRef:
                return CueScriptFieldType_EntityRef;
            case ECS::ScriptFieldType::ClassRef:
                return CueScriptFieldType_ClassRef;
            case ECS::ScriptFieldType::Float:
            default:
                return CueScriptFieldType_Float;
            }
        }

        [[nodiscard]] ECS::ScriptFieldReferenceRole to_script_field_reference_role(
            CueScriptFieldReferenceRole a_role) noexcept
        {
            switch (a_role)
            {
            case CueScriptFieldReferenceRole_ScriptReferenceEntity:
                return ECS::ScriptFieldReferenceRole::ScriptReferenceEntity;
            case CueScriptFieldReferenceRole_ScriptReferenceClass:
                return ECS::ScriptFieldReferenceRole::ScriptReferenceClass;
            case CueScriptFieldReferenceRole_None:
            default:
                return ECS::ScriptFieldReferenceRole::None;
            }
        }

        [[nodiscard]] CueScriptFieldReferenceRole to_cue_script_field_reference_role(
            ECS::ScriptFieldReferenceRole a_role) noexcept
        {
            switch (a_role)
            {
            case ECS::ScriptFieldReferenceRole::ScriptReferenceEntity:
                return CueScriptFieldReferenceRole_ScriptReferenceEntity;
            case ECS::ScriptFieldReferenceRole::ScriptReferenceClass:
                return CueScriptFieldReferenceRole_ScriptReferenceClass;
            case ECS::ScriptFieldReferenceRole::None:
            default:
                return CueScriptFieldReferenceRole_None;
            }
        }

        [[nodiscard]] MarionnettePropertyType to_marionnette_property_type(
            ECS::ScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case ECS::ScriptFieldType::Int32:
                return MarionnettePropertyType::Int32;
            case ECS::ScriptFieldType::Bool:
                return MarionnettePropertyType::Bool;
            case ECS::ScriptFieldType::EntityRef:
                return MarionnettePropertyType::EntityRef;
            case ECS::ScriptFieldType::ClassRef:
                return MarionnettePropertyType::ClassRef;
            case ECS::ScriptFieldType::Float:
            default:
                return MarionnettePropertyType::Float;
            }
        }

        [[nodiscard]] MarionnettePropertyReferenceRole
            to_marionnette_property_reference_role(
                ECS::ScriptFieldReferenceRole a_role) noexcept
        {
            switch (a_role)
            {
            case ECS::ScriptFieldReferenceRole::ScriptReferenceEntity:
                return MarionnettePropertyReferenceRole::ScriptReferenceEntity;
            case ECS::ScriptFieldReferenceRole::ScriptReferenceClass:
                return MarionnettePropertyReferenceRole::ScriptReferenceClass;
            case ECS::ScriptFieldReferenceRole::None:
            default:
                return MarionnettePropertyReferenceRole::None;
            }
        }

        [[nodiscard]] CueScriptFieldValue to_cue_script_field_value(
            const ECS::ScriptFieldValue& a_fieldValue) noexcept
        {
            return CueScriptFieldValue{
                make_string_view(a_fieldValue.name),
                to_cue_script_field_type(a_fieldValue.type),
                a_fieldValue.floatValue,
                a_fieldValue.intValue,
                static_cast<uint8_t>(a_fieldValue.boolValue ? 1 : 0),
                0,
                0,
                0,
                a_fieldValue.entityValue != GameCore::k_invalidEntityId
                    ? CueEntityHandle{
                          static_cast<uint64_t>(a_fieldValue.entityValue)
                      }
                    : CueEntityHandle{ k_cueInvalidHandleValue },
                make_string_view(a_fieldValue.classValue),
                make_string_view(a_fieldValue.groupName),
                to_cue_script_field_reference_role(a_fieldValue.referenceRole),
                static_cast<CueScriptFieldFlags>(a_fieldValue.flags)
            };
        }

        [[nodiscard]] bool are_script_field_values_equal(
            const std::vector<ECS::ScriptFieldValue>& a_left,
            const std::vector<ECS::ScriptFieldValue>& a_right) noexcept
        {
            if (a_left.size() != a_right.size())
            {
                return false;
            }

            for (size_t index = 0; index < a_left.size(); ++index)
            {
                const ECS::ScriptFieldValue& left = a_left[index];
                const ECS::ScriptFieldValue& right = a_right[index];
                if (left.name != right.name ||
                    left.type != right.type ||
                    left.floatValue != right.floatValue ||
                    left.intValue != right.intValue ||
                    left.boolValue != right.boolValue ||
                    left.entityValue != right.entityValue ||
                    left.classValue != right.classValue ||
                    left.groupName != right.groupName ||
                    left.referenceRole != right.referenceRole ||
                    left.flags != right.flags)
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool supports_instance_state_size(
            const CueScriptExports* a_exports) noexcept
        {
            return a_exports != nullptr &&
                a_exports->structSize >=
                offsetof(CueScriptExports, getScriptInstanceStateSize) +
                    sizeof(CueGetScriptInstanceStateSizeFn) &&
                a_exports->getScriptInstanceStateSize != nullptr;
        }

        [[nodiscard]] bool supports_instance_state_serialize(
            const CueScriptExports* a_exports) noexcept
        {
            return a_exports != nullptr &&
                a_exports->structSize >=
                offsetof(CueScriptExports, serializeScriptInstance) +
                    sizeof(CueSerializeScriptInstanceFn) &&
                a_exports->serializeScriptInstance != nullptr;
        }

        [[nodiscard]] bool supports_instance_state_restore(
            const CueScriptExports* a_exports) noexcept
        {
            return a_exports != nullptr &&
                a_exports->structSize >=
                offsetof(CueScriptExports, restoreScriptInstance) +
                    sizeof(CueRestoreScriptInstanceFn) &&
                a_exports->restoreScriptInstance != nullptr;
        }

        [[nodiscard]] bool supports_state_descriptor_query(
            const CueScriptExports* a_exports) noexcept
        {
            return a_exports != nullptr &&
                a_exports->structSize >=
                offsetof(CueScriptExports, getScriptStateDescriptor) +
                    sizeof(CueGetScriptStateDescriptorFn) &&
                a_exports->getScriptStateDescriptor != nullptr;
        }

        [[nodiscard]] Result query_state_descriptor(
            const CueScriptExports* a_exports,
            std::string_view a_className,
            CueScriptStateDescriptor& a_outDescriptor) noexcept
        {
            a_outDescriptor = {};
            if (!supports_state_descriptor_query(a_exports))
            {
                return Result::fail(Code::Unsupported, Severity::Warning,
                    "Script state descriptor query is not supported.");
            }

            const CueResult descriptorResult =
                a_exports->getScriptStateDescriptor(
                    make_string_view(a_className), &a_outDescriptor);
            switch (descriptorResult)
            {
            case CueResult_Ok:
                return Result::ok();

            case CueResult_InvalidArgument:
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Script state descriptor query returned InvalidArgument.");

            case CueResult_NotFound:
                return Result::fail(Code::NotFound, Severity::Warning,
                    "Script state descriptor query returned NotFound.");

            case CueResult_Unsupported:
                return Result::fail(Code::Unsupported, Severity::Warning,
                    "Script state descriptor query returned Unsupported.");

            case CueResult_InvalidState:
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Script state descriptor query returned InvalidState.");

            case CueResult_InternalError:
                return Result::fail(Code::InternalError, Severity::Error,
                    "Script state descriptor query returned InternalError.");
            }

            return Result::fail(Code::UnknownError, Severity::Error,
                "Script state descriptor query returned unknown error.");
        }

        [[nodiscard]] bool are_state_descriptors_compatible(
            const CueScriptStateDescriptor& a_left,
            const CueScriptStateDescriptor& a_right) noexcept
        {
            return a_left.stateVersion == a_right.stateVersion &&
                a_left.stateSize == a_right.stateSize &&
                a_left.schemaHash == a_right.schemaHash;
        }
    }

    ScriptRuntime* ScriptRuntime::s_activeInstance = nullptr;

    ScriptRuntime::ScriptRuntime(
        GameCore::GameWorld& a_gameWorld,
        PAL::IPlatform* a_platform) noexcept
        : m_gameWorld(&a_gameWorld)
        , m_platform(a_platform)
    {
        s_activeInstance = this;
        m_engineApi.structSize = sizeof(CueEngineApi);
        m_engineApi.abiVersion = k_cueScriptAbiVersion;
        m_engineApi.log = &ScriptRuntime::log_bridge;
        m_engineApi.registerScriptClass = &ScriptRuntime::register_script_class_bridge;
        m_engineApi.isEntityValid = &ScriptRuntime::is_entity_valid_bridge;
        m_engineApi.hasTransform = &ScriptRuntime::has_transform_bridge;
        m_engineApi.getTransform = &ScriptRuntime::get_transform_bridge;
        m_engineApi.setTransform = &ScriptRuntime::set_transform_bridge;
        m_engineApi.pushKey = &ScriptRuntime::push_key_bridge;
        m_engineApi.registerScriptField = &ScriptRuntime::register_script_field_bridge;
        m_engineApi.registerScriptFunction =
            &ScriptRuntime::register_script_function_bridge;
        m_engineApi.findScriptInstance = &ScriptRuntime::find_script_instance_bridge;
        m_engineApi.isScriptInstanceValid = &ScriptRuntime::is_script_instance_valid_bridge;
        m_engineApi.getScriptField = &ScriptRuntime::get_script_field_bridge;
        m_engineApi.getScriptObject = &ScriptRuntime::get_script_object_bridge;
        m_engineApi.isScriptClassRegistered =
            &ScriptRuntime::is_script_class_registered_bridge;
        m_engineApi.getScriptClassField =
            &ScriptRuntime::get_script_class_field_bridge;
        m_engineApi.hasScriptClassFunction =
            &ScriptRuntime::has_script_class_function_bridge;
        m_engineApi.invokeScriptFunction =
            &ScriptRuntime::invoke_script_function_bridge;
        m_engineApi.requestAudioSourcePlay =
            &ScriptRuntime::request_audio_source_play_bridge;
        m_engineApi.requestSceneLoad = &ScriptRuntime::request_scene_load_bridge;
        m_engineApi.requestSceneUnload = &ScriptRuntime::request_scene_unload_bridge;
        m_engineApi.setRigidBodyLinearVelocity =
            &ScriptRuntime::set_rigid_body_linear_velocity_bridge;
        m_engineApi.getRigidBodyLinearVelocity =
            &ScriptRuntime::get_rigid_body_linear_velocity_bridge;
        m_engineApi.addRigidBodyForce =
            &ScriptRuntime::add_rigid_body_force_bridge;
        m_engineApi.addRigidBodyImpulse =
            &ScriptRuntime::add_rigid_body_impulse_bridge;
        m_engineApi.setCharacterMoveVelocity =
            &ScriptRuntime::set_character_move_velocity_bridge;
        m_engineApi.requestCharacterJump =
            &ScriptRuntime::request_character_jump_bridge;
        m_engineApi.setNavAgentDestination =
            &ScriptRuntime::set_nav_agent_destination_bridge;
        m_engineApi.setNavAgentTarget =
            &ScriptRuntime::set_nav_agent_target_bridge;
        m_engineApi.getMouseDelta = &ScriptRuntime::get_mouse_delta_bridge;
        m_engineApi.pushMouseButton = &ScriptRuntime::push_mouse_button_bridge;
        m_engineApi.raycast = &ScriptRuntime::raycast_bridge;
        m_engineApi.debugDrawLine = &ScriptRuntime::debug_draw_line_bridge;
        m_engineApi.debugDrawSphere = &ScriptRuntime::debug_draw_sphere_bridge;
        m_engineApi.debugDrawBox = &ScriptRuntime::debug_draw_box_bridge;
        m_engineApi.getTransformDegrees =
            &ScriptRuntime::get_transform_degrees_bridge;
        m_engineApi.setTransformDegrees =
            &ScriptRuntime::set_transform_degrees_bridge;
        m_engineApi.getTransformQuaternion =
            &ScriptRuntime::get_transform_quaternion_bridge;
        m_engineApi.setTransformQuaternion =
            &ScriptRuntime::set_transform_quaternion_bridge;
        m_engineApi.spawnObject = &ScriptRuntime::spawn_object_bridge;
        m_engineApi.instantiateEntity = &ScriptRuntime::instantiate_entity_bridge;
        m_engineApi.findEntitiesByTag = &ScriptRuntime::find_entities_by_tag_bridge;
        m_engineApi.findEntitiesByName =
            &ScriptRuntime::find_entities_by_name_bridge;
        m_engineApi.triggerOverlaps = &ScriptRuntime::trigger_overlaps_bridge;
        m_engineApi.sphereOverlap = &ScriptRuntime::sphere_overlap_bridge;
        m_engineApi.destroyEntity = &ScriptRuntime::destroy_entity_bridge;
        m_engineApi.getCameraFovY = &ScriptRuntime::get_camera_fov_y_bridge;
        m_engineApi.setCameraFovY = &ScriptRuntime::set_camera_fov_y_bridge;
        m_engineApi.addOrSetComponent =
            &ScriptRuntime::add_or_set_component_bridge;
        m_engineApi.getParent = &ScriptRuntime::get_parent_bridge;
        m_engineApi.setParent = &ScriptRuntime::set_parent_bridge;
        m_engineApi.detachParent = &ScriptRuntime::detach_parent_bridge;
    }

    ScriptRuntime::~ScriptRuntime()
    {
        (void)reset();
        if (s_activeInstance == this)
        {
            s_activeInstance = nullptr;
        }
    }

    const CueEngineApi& ScriptRuntime::engine_api() const noexcept
    {
        return m_engineApi;
    }

    const std::vector<std::string>&
        ScriptRuntime::registered_script_classes() const noexcept
    {
        return m_registeredScriptClasses;
    }

    bool ScriptRuntime::has_registered_script_class(
        std::string_view a_className) const noexcept
    {
        return std::find(m_registeredScriptClasses.begin(),
                   m_registeredScriptClasses.end(),
                   a_className) != m_registeredScriptClasses.end();
    }

    const std::vector<ECS::ScriptFieldValue>&
        ScriptRuntime::script_field_defaults(std::string_view a_className) const noexcept
    {
        static const std::vector<ECS::ScriptFieldValue> k_emptyFieldValues{};

        const auto infoIt = m_scriptClassInfos.find(std::string(a_className));
        if (infoIt == m_scriptClassInfos.end())
        {
            return k_emptyFieldValues;
        }

        return infoIt->second.fieldDefaults;
    }

    const MarionnetteClass* ScriptRuntime::find_marionnette_class(
        std::string_view a_className) const noexcept
    {
        const auto infoIt = m_scriptClassInfos.find(std::string(a_className));
        if (infoIt == m_scriptClassInfos.end())
        {
            return nullptr;
        }

        return &infoIt->second.marionnetteClass;
    }

    const MarionnetteClass* ScriptRuntime::find_marionnette_class(
        GameCore::EntityId a_entityId) const noexcept
    {
        const auto bindingIt = m_bindings.find(a_entityId);
        if (bindingIt != m_bindings.end())
        {
            return find_marionnette_class(bindingIt->second.className);
        }

        if (m_gameWorld == nullptr)
        {
            return nullptr;
        }

        GameCore::GameObject object{};
        const Result findResult = m_gameWorld->find_object(a_entityId, object);
        if (!findResult || !object.is_valid())
        {
            return nullptr;
        }

        ECS::ScriptComponent* script = nullptr;
        const Result getResult = object.get_component(script);
        if (!getResult || script == nullptr || script->className.empty())
        {
            return nullptr;
        }

        return find_marionnette_class(script->className);
    }

    Marionnette* ScriptRuntime::find_marionnette(
        GameCore::EntityId a_entityId) noexcept
    {
        const auto marionnetteIt = m_marionnettes.find(a_entityId);
        return marionnetteIt != m_marionnettes.end()
            ? &marionnetteIt->second
            : nullptr;
    }

    const Marionnette* ScriptRuntime::find_marionnette(
        GameCore::EntityId a_entityId) const noexcept
    {
        const auto marionnetteIt = m_marionnettes.find(a_entityId);
        return marionnetteIt != m_marionnettes.end()
            ? &marionnetteIt->second
            : nullptr;
    }

    MarionnetteComponent* ScriptRuntime::find_marionnette_component(
        GameCore::EntityId a_entityId) noexcept
    {
        const auto componentIt = m_marionnetteComponents.find(a_entityId);
        return componentIt != m_marionnetteComponents.end()
            ? componentIt->second.get()
            : nullptr;
    }

    const MarionnetteComponent* ScriptRuntime::find_marionnette_component(
        GameCore::EntityId a_entityId) const noexcept
    {
        const auto componentIt = m_marionnetteComponents.find(a_entityId);
        return componentIt != m_marionnetteComponents.end()
            ? componentIt->second.get()
            : nullptr;
    }

    MarionnetteComponent* ScriptRuntime::find_marionnette_component_by_class(
        GameCore::EntityId a_entityId,
        const MarionnetteClass* a_componentClass) noexcept
    {
        MarionnetteComponent* component = find_marionnette_component(a_entityId);
        if (component == nullptr)
        {
            return nullptr;
        }

        if (a_componentClass == nullptr || component->is_a(a_componentClass))
        {
            return component;
        }

        return nullptr;
    }

    const MarionnetteComponent* ScriptRuntime::find_marionnette_component_by_class(
        GameCore::EntityId a_entityId,
        const MarionnetteClass* a_componentClass) const noexcept
    {
        const MarionnetteComponent* component =
            find_marionnette_component(a_entityId);
        if (component == nullptr)
        {
            return nullptr;
        }

        if (a_componentClass == nullptr || component->is_a(a_componentClass))
        {
            return component;
        }

        return nullptr;
    }

    void ScriptRuntime::activate() noexcept
    {
        s_activeInstance = this;
    }

    void ScriptRuntime::set_module(const ScriptModule* a_module) noexcept
    {
        m_module = a_module;
        m_registeredScriptClasses.clear();
        m_scriptClassInfos.clear();
    }

    Result ScriptRuntime::set_game_world(GameCore::GameWorld& a_gameWorld) noexcept
    {
        Result result = reset();
        if (!result)
        {
            return result;
        }

        m_gameWorld = &a_gameWorld;
        return Result::ok();
    }

    Result ScriptRuntime::capture_instance_states(
        std::vector<StateSnapshot>& a_outSnapshots) const noexcept
    {
        a_outSnapshots.clear();

        if (!ScriptModule::is_loaded(m_module))
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (!supports_instance_state_size(exports) ||
            !supports_instance_state_serialize(exports))
        {
            return Result::ok();
        }

        a_outSnapshots.reserve(m_bindings.size());
        for (const auto& [entityId, binding] : m_bindings)
        {
            StateSnapshot snapshot{};
            snapshot.entityId = entityId;
            snapshot.className = binding.className;

            Result descriptorResult = query_state_descriptor(
                exports, binding.className, snapshot.stateDescriptor);
            if (descriptorResult)
            {
                snapshot.hasStateDescriptor = true;
            }
            else
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state descriptor capture unavailable: entity={}, class={}, message={}",
                    entityId, binding.className, descriptorResult.message);
            }

            uint32_t stateSize = 0;
            Result sizeResult = convert_script_result(
                exports->getScriptInstanceStateSize(
                    binding.instanceHandle, &stateSize));
            if (!sizeResult)
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state size capture skipped: entity={}, class={}, message={}",
                    entityId, binding.className, sizeResult.message);
                continue;
            }

            if (stateSize > 0)
            {
                snapshot.bytes.resize(stateSize);
                Result serializeResult = convert_script_result(
                    exports->serializeScriptInstance(
                        binding.instanceHandle,
                        snapshot.bytes.data(),
                        stateSize));
                if (!serializeResult)
                {
                    Core::IO::log(Core::IO::LogSink::debugConsole,
                        "Script state serialize skipped: entity={}, class={}, message={}",
                        entityId, binding.className, serializeResult.message);
                    continue;
                }
            }

            a_outSnapshots.push_back(std::move(snapshot));
        }

        return Result::ok();
    }

    Result ScriptRuntime::restore_instance_states(
        const std::vector<StateSnapshot>& a_snapshots,
        StateRestoreReport* a_outReport) noexcept
    {
        if (a_outReport != nullptr)
        {
            *a_outReport = {};
        }

        if (a_snapshots.empty())
        {
            return Result::ok();
        }
        if (!ScriptModule::is_loaded(m_module))
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (!supports_instance_state_restore(exports))
        {
            return Result::ok();
        }

        Result syncResult = sync_instances();
        if (!syncResult)
        {
            return syncResult;
        }

        const auto record_skip =
            [a_outReport](GameCore::EntityId a_entityId,
                std::string_view a_className,
                std::string a_detail)
        {
            if (a_outReport == nullptr)
            {
                return;
            }

            ++a_outReport->skippedCount;
            a_outReport->issues.push_back(StateRestoreIssue{
                a_entityId,
                std::string(a_className),
                std::move(a_detail)
            });
        };

        for (const StateSnapshot& snapshot : a_snapshots)
        {
            const auto bindingIt = m_bindings.find(snapshot.entityId);
            if (bindingIt == m_bindings.end())
            {
                continue;
            }
            if (bindingIt->second.className != snapshot.className)
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state restore skipped by class mismatch: entity={}, old={}, new={}",
                    snapshot.entityId, snapshot.className, bindingIt->second.className);
                record_skip(
                    snapshot.entityId,
                    snapshot.className,
                    std::string("class mismatch: old=") + snapshot.className +
                        ", new=" + bindingIt->second.className);
                continue;
            }
            if (!snapshot.hasStateDescriptor)
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state restore skipped without old descriptor: entity={}, class={}",
                    snapshot.entityId, snapshot.className);
                record_skip(
                    snapshot.entityId,
                    snapshot.className,
                    "old descriptor is unavailable");
                continue;
            }
            if (snapshot.bytes.empty())
            {
                continue;
            }

            CueScriptStateDescriptor nextDescriptor{};
            Result descriptorResult = query_state_descriptor(
                exports, bindingIt->second.className, nextDescriptor);
            if (!descriptorResult)
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state restore skipped without new descriptor: entity={}, class={}, message={}",
                    snapshot.entityId, snapshot.className, descriptorResult.message);
                record_skip(
                    snapshot.entityId,
                    snapshot.className,
                    std::string("new descriptor query failed: ") +
                        std::string(descriptorResult.message));
                continue;
            }
            if (!are_state_descriptors_compatible(
                    snapshot.stateDescriptor, nextDescriptor))
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state restore skipped by descriptor mismatch: entity={}, class={}, oldVersion={}, newVersion={}, oldSize={}, newSize={}, oldHash={}, newHash={}",
                    snapshot.entityId, snapshot.className,
                    snapshot.stateDescriptor.stateVersion,
                    nextDescriptor.stateVersion,
                    snapshot.stateDescriptor.stateSize,
                    nextDescriptor.stateSize,
                    snapshot.stateDescriptor.schemaHash,
                    nextDescriptor.schemaHash);
                record_skip(
                    snapshot.entityId,
                    snapshot.className,
                    std::string("descriptor mismatch: oldVersion=") +
                        std::to_string(snapshot.stateDescriptor.stateVersion) +
                        ", newVersion=" +
                        std::to_string(nextDescriptor.stateVersion) +
                        ", oldSize=" +
                        std::to_string(snapshot.stateDescriptor.stateSize) +
                        ", newSize=" +
                        std::to_string(nextDescriptor.stateSize) +
                        ", oldHash=" +
                        std::to_string(snapshot.stateDescriptor.schemaHash) +
                        ", newHash=" +
                        std::to_string(nextDescriptor.schemaHash));
                continue;
            }

            Result restoreResult = convert_script_result(
                exports->restoreScriptInstance(
                    bindingIt->second.instanceHandle,
                    snapshot.bytes.data(),
                    static_cast<uint32_t>(snapshot.bytes.size())));
            if (!restoreResult)
            {
                Core::IO::log(Core::IO::LogSink::debugConsole,
                    "Script state restore skipped: entity={}, class={}, message={}",
                    snapshot.entityId, snapshot.className, restoreResult.message);
                record_skip(
                    snapshot.entityId,
                    snapshot.className,
                    std::string("restore failed: ") +
                        std::string(restoreResult.message));
                continue;
            }

            if (a_outReport != nullptr)
            {
                ++a_outReport->restoredCount;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::update(float a_deltaTimeSeconds) noexcept
    {
        if (m_gameWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime game world is not initialized.");
        }

        Result result = m_gameWorld->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        result = sync_instances();
        if (!result)
        {
            return result;
        }

        if (!ScriptModule::is_loaded(m_module))
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr || exports->updateScriptInstance == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime module exports are invalid.");
        }

        std::vector<GameCore::EntityId> staleEntities{};
        staleEntities.reserve(m_bindings.size());

        for (const auto& [entityId, binding] : m_bindings)
        {
            m_executingEntityId = entityId;
            const CueResult updateResult =
                exports->updateScriptInstance(binding.instanceHandle, a_deltaTimeSeconds);
            m_executingEntityId = GameCore::k_invalidEntityId;
            if (updateResult == CueResult_Ok)
            {
                continue;
            }

            if (updateResult == CueResult_NotFound)
            {
                staleEntities.push_back(entityId);
                continue;
            }

            return convert_script_result(updateResult);
        }

        for (const GameCore::EntityId entityId : staleEntities)
        {
            result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::dispatch_collision_events() noexcept
    {
        if (m_gameWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime game world is not initialized.");
        }
        if (!ScriptModule::is_loaded(m_module))
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime module exports are invalid.");
        }
        if (exports->structSize <
                offsetof(CueScriptExports, dispatchScriptCollisionEvent) +
                    sizeof(CueDispatchScriptCollisionEventFn) ||
            exports->dispatchScriptCollisionEvent == nullptr)
        {
            return Result::ok();
        }

        for (const auto& [entityId, binding] : m_bindings)
        {
            ECS::TriggerVolumeComponent* mutableTrigger = nullptr;
            Result triggerResult =
                m_gameWorld->get_component(entityId, mutableTrigger);
            const ECS::TriggerVolumeComponent* trigger =
                mutableTrigger;
            if (!triggerResult || trigger == nullptr)
            {
                continue;
            }

            const auto dispatchEvent =
                [&exports, &binding](
                    CueScriptCollisionEventType a_eventType,
                    GameCore::EntityId a_otherEntity) -> Result
            {
                if (a_otherEntity == GameCore::k_invalidEntityId)
                {
                    return Result::ok();
                }

                const CueResult scriptResult =
                    exports->dispatchScriptCollisionEvent(
                        binding.instanceHandle,
                        a_eventType,
                        to_entity_handle(a_otherEntity));
                if (scriptResult == CueResult_Ok ||
                    scriptResult == CueResult_NotFound)
                {
                    return Result::ok();
                }

                return convert_script_result(scriptResult);
            };

            for (const GameCore::EntityId otherEntity : trigger->enteredEntities)
            {
                Result eventResult = dispatchEvent(
                    CueScriptCollisionEventType_Enter, otherEntity);
                if (!eventResult)
                {
                    return eventResult;
                }
            }
            for (const GameCore::EntityId otherEntity :
                trigger->overlappingEntities)
            {
                Result eventResult = dispatchEvent(
                    CueScriptCollisionEventType_Stay, otherEntity);
                if (!eventResult)
                {
                    return eventResult;
                }
            }
            for (const GameCore::EntityId otherEntity : trigger->exitedEntities)
            {
                Result eventResult = dispatchEvent(
                    CueScriptCollisionEventType_Exit, otherEntity);
                if (!eventResult)
                {
                    return eventResult;
                }
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::reset() noexcept
    {
        return destroy_all_instances();
    }

    Result ScriptRuntime::sync_instances() noexcept
    {
        std::unordered_set<GameCore::EntityId> desiredEntities{};
        desiredEntities.reserve(m_bindings.size() + 8);
        Result syncResult = Result::ok();

        Result enumerateResult = m_gameWorld->for_each_object(
            [this, &desiredEntities, &syncResult](GameCore::EntityId a_entityId,
                GameCore::SceneId, GameCore::GameObject& a_object)
            {
                if (!syncResult)
                {
                    return;
                }

                bool hasScript = false;
                Result hasResult =
                    a_object.has_component<ECS::ScriptComponent>(hasScript);
                if (!hasResult || !hasScript)
                {
                    return;
                }

                ECS::ScriptComponent* script = nullptr;
                Result getResult = a_object.get_component(script);
                if (!getResult || script == nullptr || !script->isEnabled ||
                    script->className.empty())
                {
                    return;
                }

                desiredEntities.insert(a_entityId);
                const std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
                    resolve_script_field_values(*script);

                const auto bindingIt = m_bindings.find(a_entityId);
                if (bindingIt != m_bindings.end() &&
                    bindingIt->second.className == script->className &&
                    are_script_field_values_equal(
                        bindingIt->second.fieldValues,
                        resolvedFieldValues))
                {
                    return;
                }

                if (bindingIt != m_bindings.end())
                {
                    syncResult = destroy_instance(a_entityId);
                    if (!syncResult)
                    {
                        return;
                    }
                }

                syncResult = create_instance(
                    a_entityId, *script);
                if (!syncResult)
                {
                    return;
                }
            });
        if (!enumerateResult)
        {
            return enumerateResult;
        }
        if (!syncResult)
        {
            return syncResult;
        }

        std::vector<GameCore::EntityId> removedEntities{};
        removedEntities.reserve(m_bindings.size());
        for (const auto& [entityId, _] : m_bindings)
        {
            if (!desiredEntities.contains(entityId))
            {
                removedEntities.push_back(entityId);
            }
        }

        for (const GameCore::EntityId entityId : removedEntities)
        {
            Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::create_instance(
        GameCore::EntityId a_entityId,
        const ECS::ScriptComponent& a_scriptComponent) noexcept
    {
        if (!ScriptModule::is_loaded(m_module))
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr || exports->createScriptInstance == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime module exports are invalid.");
        }

        CueScriptCreateInfo createInfo{};
        createInfo.entityHandle = to_entity_handle(a_entityId);
        createInfo.scriptName = make_string_view(a_scriptComponent.className);

        std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
            resolve_script_field_values(a_scriptComponent);

        std::vector<CueScriptFieldValue> fieldValues{};
        fieldValues.reserve(resolvedFieldValues.size());
        for (const ECS::ScriptFieldValue& fieldValue : resolvedFieldValues)
        {
            fieldValues.push_back(to_cue_script_field_value(fieldValue));
        }
        createInfo.fieldValues = fieldValues.empty() ? nullptr : fieldValues.data();
        createInfo.fieldValueCount = static_cast<uint32_t>(fieldValues.size());

        CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
        const CueResult createResult =
            exports->createScriptInstance(&createInfo, &instanceHandle);
        Result result = convert_script_result(createResult);
        if (!result)
        {
            return result;
        }

        result = bind_marionnette(a_entityId);
        if (!result)
        {
            (void)exports->destroyScriptInstance(instanceHandle);
            return result;
        }

        result = bind_marionnette_component(a_entityId, a_scriptComponent);
        if (!result)
        {
            unbind_marionnette(a_entityId);
            (void)exports->destroyScriptInstance(instanceHandle);
            return result;
        }

        m_bindings[a_entityId] = ScriptBinding{
            instanceHandle,
            a_scriptComponent.className,
            resolvedFieldValues,
            m_module->get_script_instance_object(instanceHandle)
        };
        m_entityIdsByInstanceHandle[instanceHandle.value] = a_entityId;
        return Result::ok();
    }

    std::vector<ECS::ScriptFieldValue> ScriptRuntime::resolve_script_field_values(
        const ECS::ScriptComponent& a_scriptComponent) const
    {
        std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
            script_field_defaults(a_scriptComponent.className);

        const auto merge_field_values =
            [&resolvedFieldValues](
                const std::vector<ECS::ScriptFieldValue>& a_fieldValues)
        {
            for (const ECS::ScriptFieldValue& fieldValue : a_fieldValues)
            {
                const auto existingIt =
                    std::find_if(resolvedFieldValues.begin(), resolvedFieldValues.end(),
                        [&fieldValue](const ECS::ScriptFieldValue& a_existingValue)
                        {
                            return a_existingValue.name == fieldValue.name;
                        });
                if (existingIt != resolvedFieldValues.end())
                {
                    *existingIt = fieldValue;
                    continue;
                }

                resolvedFieldValues.push_back(fieldValue);
            }
        };

        merge_field_values(a_scriptComponent.serializedFieldValues);
        merge_field_values(a_scriptComponent.transientFieldValues);

        return resolvedFieldValues;
    }

    Result ScriptRuntime::destroy_instance(GameCore::EntityId a_entityId) noexcept
    {
        const auto bindingIt = m_bindings.find(a_entityId);
        if (bindingIt == m_bindings.end())
        {
            unbind_marionnette_component(a_entityId);
            unbind_marionnette(a_entityId);
            return Result::ok();
        }

        if (ScriptModule::is_loaded(m_module))
        {
            const CueScriptExports* exports = m_module->exports();
            if (exports == nullptr || exports->destroyScriptInstance == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Script runtime module exports are invalid.");
            }

            const CueResult destroyResult =
                exports->destroyScriptInstance(bindingIt->second.instanceHandle);
            Result result = convert_script_result(destroyResult);
            if (!result && result.code != Code::NotFound)
            {
                return result;
            }
        }

        m_entityIdsByInstanceHandle.erase(bindingIt->second.instanceHandle.value);
        m_bindings.erase(bindingIt);
        unbind_marionnette_component(a_entityId);
        unbind_marionnette(a_entityId);
        return Result::ok();
    }

    Result ScriptRuntime::destroy_all_instances() noexcept
    {
        std::vector<GameCore::EntityId> entityIds{};
        entityIds.reserve(m_bindings.size());
        for (const auto& [entityId, _] : m_bindings)
        {
            entityIds.push_back(entityId);
        }

        for (const GameCore::EntityId entityId : entityIds)
        {
            Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::log_bridge(
        CueLogSeverity a_severity,
        CueStringView a_message)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->log_internal(a_severity, a_message)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::register_script_class_bridge(
        CueStringView a_scriptClassName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->register_script_class_internal(a_scriptClassName)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::register_script_field_bridge(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->register_script_field_internal(
                a_scriptClassName, a_fieldValue)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::register_script_function_bridge(
        CueStringView a_scriptClassName,
        const CueScriptFunctionDefinition* a_functionDefinition)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->register_script_function_internal(
                a_scriptClassName, a_functionDefinition)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::is_entity_valid_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->is_entity_valid_internal(a_entityHandle)
            : 0;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::has_transform_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->has_transform_internal(a_entityHandle)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_transform_bridge(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_transform_internal(
                a_entityHandle, a_outTransform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_transform_bridge(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_transform_internal(
                a_entityHandle, a_transform)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::push_key_bridge(CueKey a_key)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->push_key_internal(a_key)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::find_script_instance_bridge(
        CueEntityHandle a_entityHandle,
        CueStringView a_scriptClassName,
        CueScriptInstanceHandle* a_outInstanceHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->find_script_instance_internal(
                a_entityHandle, a_scriptClassName, a_outInstanceHandle)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::is_script_instance_valid_bridge(
        CueScriptInstanceHandle a_instanceHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->is_script_instance_valid_internal(a_instanceHandle)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_script_field_bridge(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_script_field_internal(
                a_instanceHandle, a_fieldName, a_outFieldValue)
            : CueResult_InvalidState;
    }

    void* CUE_SCRIPT_CALL ScriptRuntime::get_script_object_bridge(
        CueScriptInstanceHandle a_instanceHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_script_object_internal(a_instanceHandle)
            : nullptr;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::is_script_class_registered_bridge(
        CueStringView a_scriptClassName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->is_script_class_registered_internal(
                a_scriptClassName)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_script_class_field_bridge(
        CueStringView a_scriptClassName,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_script_class_field_internal(
                a_scriptClassName, a_fieldName, a_outFieldValue)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::has_script_class_function_bridge(
        CueStringView a_scriptClassName,
        CueStringView a_functionName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->has_script_class_function_internal(
                a_scriptClassName, a_functionName)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::invoke_script_function_bridge(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_functionName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->invoke_script_function_internal(
                a_instanceHandle, a_functionName)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::request_audio_source_play_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->request_audio_source_play_internal(a_entityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_transform_degrees_bridge(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_transform_degrees_internal(
                a_entityHandle, a_outTransform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_transform_degrees_bridge(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_transform_degrees_internal(
                a_entityHandle, a_transform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_transform_quaternion_bridge(
        CueEntityHandle a_entityHandle,
        CueTransformQuaternionData* a_outTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_transform_quaternion_internal(
                a_entityHandle, a_outTransform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_transform_quaternion_bridge(
        CueEntityHandle a_entityHandle,
        const CueTransformQuaternionData* a_transform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_transform_quaternion_internal(
                a_entityHandle, a_transform)
            : CueResult_InvalidState;
    }

    CueSceneId CUE_SCRIPT_CALL ScriptRuntime::request_scene_load_bridge(
        CueStringView a_sceneName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->request_scene_load_internal(a_sceneName)
            : k_cueInvalidSceneId;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::request_scene_unload_bridge(
        CueSceneId a_sceneId)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->request_scene_unload_internal(a_sceneId)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL
        ScriptRuntime::set_rigid_body_linear_velocity_bridge(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_velocity)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_rigid_body_linear_velocity_internal(
                a_entityHandle, a_velocity)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL
        ScriptRuntime::get_rigid_body_linear_velocity_bridge(
            CueEntityHandle a_entityHandle,
            CueFloat3* a_outVelocity)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_rigid_body_linear_velocity_internal(
                a_entityHandle, a_outVelocity)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::add_rigid_body_force_bridge(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_force)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->add_rigid_body_force_internal(
                a_entityHandle, a_force)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::add_rigid_body_impulse_bridge(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_impulse)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->add_rigid_body_impulse_internal(
                a_entityHandle, a_impulse)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL
        ScriptRuntime::set_character_move_velocity_bridge(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_velocity)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_character_move_velocity_internal(
                a_entityHandle, a_velocity)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::request_character_jump_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->request_character_jump_internal(a_entityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_nav_agent_destination_bridge(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_destination)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_nav_agent_destination_internal(
                a_entityHandle, a_destination)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_nav_agent_target_bridge(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_targetEntityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_nav_agent_target_internal(
                a_entityHandle, a_targetEntityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_mouse_delta_bridge(
        CueMouseDeltaData* a_outDelta)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_mouse_delta_internal(a_outDelta)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::push_mouse_button_bridge(
        CueMouseButton a_button)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->push_mouse_button_internal(a_button)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::raycast_bridge(
        const CueRaycastDesc* a_desc,
        CueRaycastHit* a_outHit)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->raycast_internal(a_desc, a_outHit)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::debug_draw_line_bridge(
        const CueFloat3* a_start,
        const CueFloat3* a_end,
        const CueFloat4* a_color,
        float a_durationSeconds)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->debug_draw_line_internal(
                a_start, a_end, a_color, a_durationSeconds)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::debug_draw_sphere_bridge(
        const CueFloat3* a_center,
        float a_radius,
        const CueFloat4* a_color,
        float a_durationSeconds)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->debug_draw_sphere_internal(
                a_center, a_radius, a_color, a_durationSeconds)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::debug_draw_box_bridge(
        const CueFloat3* a_center,
        const CueFloat3* a_halfExtent,
        const CueFloat4* a_color,
        float a_durationSeconds)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->debug_draw_box_internal(
                a_center, a_halfExtent, a_color, a_durationSeconds)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::spawn_object_bridge(
        const CueSpawnObjectDesc* a_desc,
        CueEntityHandle* a_outEntityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->spawn_object_internal(a_desc, a_outEntityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::instantiate_entity_bridge(
        const CueInstantiateEntityDesc* a_desc,
        CueEntityHandle* a_outEntityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->instantiate_entity_internal(
                a_desc, a_outEntityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::find_entities_by_tag_bridge(
        CueStringView a_tag,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->find_entities_by_tag_internal(
                a_tag, a_outEntityHandles, a_capacity, a_outCount)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::find_entities_by_name_bridge(
        CueStringView a_name,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->find_entities_by_name_internal(
                a_name, a_outEntityHandles, a_capacity, a_outCount)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::trigger_overlaps_bridge(
        CueEntityHandle a_triggerEntity,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->trigger_overlaps_internal(
                a_triggerEntity, a_outEntityHandles, a_capacity, a_outCount)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::sphere_overlap_bridge(
        const CueSphereOverlapDesc* a_desc,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->sphere_overlap_internal(
                a_desc, a_outEntityHandles, a_capacity, a_outCount)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::destroy_entity_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->destroy_entity_internal(a_entityHandle)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_camera_fov_y_bridge(
        CueEntityHandle a_entityHandle,
        float* a_outFovY)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_camera_fov_y_internal(
                a_entityHandle, a_outFovY)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_camera_fov_y_bridge(
        CueEntityHandle a_entityHandle,
        float a_fovY)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_camera_fov_y_internal(
                a_entityHandle, a_fovY)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::add_or_set_component_bridge(
        CueEntityHandle a_entityHandle,
        CueComponentKind a_componentKind,
        const void* a_componentData,
        uint32_t a_componentDataSize)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->add_or_set_component_internal(
                a_entityHandle,
                a_componentKind,
                a_componentData,
                a_componentDataSize)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_parent_bridge(
        CueEntityHandle a_entityHandle,
        CueEntityHandle* a_outParentEntity)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_parent_internal(
                a_entityHandle, a_outParentEntity)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_parent_bridge(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_parentEntity,
        uint8_t a_keepsWorldTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_parent_internal(
                a_entityHandle, a_parentEntity, a_keepsWorldTransform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::detach_parent_bridge(
        CueEntityHandle a_entityHandle,
        uint8_t a_keepsWorldTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->detach_parent_internal(
                a_entityHandle, a_keepsWorldTransform)
            : CueResult_InvalidState;
    }

    CueResult ScriptRuntime::log_internal(
        CueLogSeverity a_severity,
        CueStringView a_message) noexcept
    {
        const std::string_view message =
            a_message.data != nullptr
            ? std::string_view(a_message.data, a_message.size)
            : std::string_view{};

        switch (a_severity)
        {
        case CueLogSeverity_Info:
            Core::IO::log(Core::IO::LogSink::debugConsole, "[Script] {}", message);
            return CueResult_Ok;

        case CueLogSeverity_Warning:
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "[Script][Warning] {}", message);
            return CueResult_Ok;

        case CueLogSeverity_Error:
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "[Script][Error] {}", message);
            return CueResult_Ok;
        }

        return CueResult_InvalidArgument;
    }

    CueResult ScriptRuntime::register_script_class_internal(
        CueStringView a_scriptClassName) noexcept
    {
        if (a_scriptClassName.data == nullptr || a_scriptClassName.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const std::string className(
            a_scriptClassName.data, a_scriptClassName.size);
        if (!m_scriptClassInfos.contains(className))
        {
            ScriptClassInfo classInfo{};
            classInfo.className = className;
            refresh_marionnette_class_info(classInfo);
            m_scriptClassInfos.emplace(className, std::move(classInfo));
        }
        if (has_registered_script_class(className))
        {
            return CueResult_Ok;
        }

        m_registeredScriptClasses.push_back(className);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::register_script_field_internal(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue) noexcept
    {
        if (a_fieldValue == nullptr ||
            a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0 ||
            a_fieldValue->name.data == nullptr ||
            a_fieldValue->name.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const std::string className(
            a_scriptClassName.data, a_scriptClassName.size);
        const CueResult registerClassResult =
            register_script_class_internal(a_scriptClassName);
        if (registerClassResult != CueResult_Ok)
        {
            return registerClassResult;
        }

        auto infoIt = m_scriptClassInfos.find(className);
        if (infoIt == m_scriptClassInfos.end())
        {
            return CueResult_InternalError;
        }

        ECS::ScriptFieldValue nextFieldValue{};
        nextFieldValue.name.assign(
            a_fieldValue->name.data,
            a_fieldValue->name.size);
        nextFieldValue.type = to_script_field_type(a_fieldValue->type);
        nextFieldValue.floatValue = a_fieldValue->floatValue;
        nextFieldValue.intValue = a_fieldValue->intValue;
        nextFieldValue.boolValue = a_fieldValue->boolValue != 0;
        nextFieldValue.entityValue = to_entity_id(a_fieldValue->entityValue);
        nextFieldValue.classValue.assign(
            a_fieldValue->classValue.data != nullptr
                ? a_fieldValue->classValue.data
                : "",
            a_fieldValue->classValue.data != nullptr
                ? a_fieldValue->classValue.size
                : 0u);
        nextFieldValue.groupName.assign(
            a_fieldValue->groupName.data != nullptr
                ? a_fieldValue->groupName.data
                : "",
            a_fieldValue->groupName.data != nullptr
                ? a_fieldValue->groupName.size
                : 0u);
        nextFieldValue.referenceRole =
            to_script_field_reference_role(a_fieldValue->referenceRole);
        nextFieldValue.flags =
            static_cast<ECS::ScriptFieldFlags>(a_fieldValue->flags);

        std::vector<ECS::ScriptFieldValue>& fieldDefaults =
            infoIt->second.fieldDefaults;
        const auto existingIt =
            std::find_if(fieldDefaults.begin(), fieldDefaults.end(),
                [&nextFieldValue](const ECS::ScriptFieldValue& a_value)
                {
                    return a_value.name == nextFieldValue.name;
                });
        if (existingIt != fieldDefaults.end())
        {
            *existingIt = std::move(nextFieldValue);
            refresh_marionnette_class_info(infoIt->second);
            return CueResult_Ok;
        }

        fieldDefaults.push_back(std::move(nextFieldValue));
        refresh_marionnette_class_info(infoIt->second);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::register_script_function_internal(
        CueStringView a_scriptClassName,
        const CueScriptFunctionDefinition* a_functionDefinition) noexcept
    {
        if (a_functionDefinition == nullptr ||
            a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0 ||
            a_functionDefinition->name.data == nullptr ||
            a_functionDefinition->name.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const std::string className(
            a_scriptClassName.data, a_scriptClassName.size);
        const CueResult registerClassResult =
            register_script_class_internal(a_scriptClassName);
        if (registerClassResult != CueResult_Ok)
        {
            return registerClassResult;
        }

        auto infoIt = m_scriptClassInfos.find(className);
        if (infoIt == m_scriptClassInfos.end())
        {
            return CueResult_InternalError;
        }

        const std::string functionName(
            a_functionDefinition->name.data,
            a_functionDefinition->name.size);
        std::vector<std::string>& functionNames = infoIt->second.functionNames;
        std::vector<MarionnetteFunction>& functions = infoIt->second.functions;
        const auto existingIt =
            std::find_if(functionNames.begin(), functionNames.end(),
                [&functionName](const std::string& a_value)
                {
                    return a_value == functionName;
                });
        if (existingIt != functionNames.end())
        {
            const size_t functionIndex =
                static_cast<size_t>(std::distance(functionNames.begin(), existingIt));
            if (functionIndex >= functions.size())
            {
                return CueResult_InternalError;
            }

            functions[functionIndex].flags = a_functionDefinition->flags;
            refresh_marionnette_class_info(infoIt->second);
            return CueResult_Ok;
        }

        functionNames.push_back(functionName);
        MarionnetteFunction nextFunction{};
        nextFunction.flags = a_functionDefinition->flags;
        functions.push_back(nextFunction);
        refresh_marionnette_class_info(infoIt->second);
        return CueResult_Ok;
    }

    void ScriptRuntime::refresh_marionnette_class_info(
        ScriptClassInfo& a_classInfo) noexcept
    {
        a_classInfo.properties.clear();
        a_classInfo.properties.reserve(a_classInfo.fieldDefaults.size());

        for (const ECS::ScriptFieldValue& fieldValue : a_classInfo.fieldDefaults)
        {
            MarionnetteProperty property{};
            property.name = fieldValue.name.c_str();
            property.type = to_marionnette_property_type(fieldValue.type);
            property.offset = 0;
            property.flags =
                static_cast<MarionnettePropertyFlags>(fieldValue.flags);
            property.groupName =
                fieldValue.groupName.empty() ? nullptr : fieldValue.groupName.c_str();
            property.referenceRole =
                to_marionnette_property_reference_role(fieldValue.referenceRole);
            a_classInfo.properties.push_back(property);
        }

        if (a_classInfo.functions.size() != a_classInfo.functionNames.size())
        {
            a_classInfo.functions.resize(a_classInfo.functionNames.size());
        }

        for (size_t functionIndex = 0;
             functionIndex < a_classInfo.functionNames.size();
             ++functionIndex)
        {
            a_classInfo.functions[functionIndex].name =
                a_classInfo.functionNames[functionIndex].c_str();
        }

        a_classInfo.marionnetteClass.name = a_classInfo.className.c_str();
        a_classInfo.marionnetteClass.parent =
            MarionnetteComponent::static_class();
        a_classInfo.marionnetteClass.createInstance = nullptr;
        a_classInfo.marionnetteClass.properties =
            a_classInfo.properties.empty() ? nullptr : a_classInfo.properties.data();
        a_classInfo.marionnetteClass.propertyCount =
            static_cast<uint32_t>(a_classInfo.properties.size());
        a_classInfo.marionnetteClass.functions =
            a_classInfo.functions.empty() ? nullptr : a_classInfo.functions.data();
        a_classInfo.marionnetteClass.functionCount =
            static_cast<uint32_t>(a_classInfo.functions.size());
    }

    Result ScriptRuntime::bind_marionnette(GameCore::EntityId a_entityId) noexcept
    {
        if (m_gameWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime game world is not initialized.");
        }

        GameCore::GameObject object{};
        Result result = m_gameWorld->find_object(a_entityId, object);
        if (!result || !object.is_valid())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                "Script runtime marionnette target was not found.");
        }

        Marionnette marionnette{};
        marionnette.bind(this, m_gameWorld, a_entityId, object.generation());
        m_marionnettes[a_entityId] = marionnette;
        return Result::ok();
    }

    Result ScriptRuntime::bind_marionnette_component(
        GameCore::EntityId a_entityId,
        const ECS::ScriptComponent& a_scriptComponent) noexcept
    {
        const MarionnetteClass* runtimeClass =
            find_marionnette_class(a_scriptComponent.className);
        if (runtimeClass == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                "Script runtime marionnette class was not found.");
        }

        Marionnette* owner = find_marionnette(a_entityId);
        if (owner == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime marionnette owner is not bound.");
        }

        GameCore::GameObject object{};
        Result result = m_gameWorld->find_object(a_entityId, object);
        if (!result || !object.is_valid())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                "Script runtime marionnette component target was not found.");
        }

        auto component = std::make_unique<RuntimeMarionnetteComponent>();
        component->set_runtime_class(runtimeClass);
        component->set_enabled(a_scriptComponent.isEnabled);
        component->bind(this, m_gameWorld, a_entityId, object.generation(), owner);
        m_marionnetteComponents[a_entityId] = std::move(component);
        return Result::ok();
    }

    void ScriptRuntime::unbind_marionnette(GameCore::EntityId a_entityId) noexcept
    {
        const auto marionnetteIt = m_marionnettes.find(a_entityId);
        if (marionnetteIt == m_marionnettes.end())
        {
            return;
        }

        marionnetteIt->second.unbind();
        m_marionnettes.erase(marionnetteIt);
    }

    void ScriptRuntime::unbind_marionnette_component(
        GameCore::EntityId a_entityId) noexcept
    {
        const auto componentIt = m_marionnetteComponents.find(a_entityId);
        if (componentIt == m_marionnetteComponents.end())
        {
            return;
        }

        componentIt->second->unbind();
        m_marionnetteComponents.erase(componentIt);
    }

    uint8_t ScriptRuntime::is_entity_valid_internal(
        CueEntityHandle a_entityHandle) const noexcept
    {
        if (m_gameWorld == nullptr || a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return 0;
        }

        bool containsObject = false;
        const Result result = m_gameWorld->contains_object(
            to_entity_id(a_entityHandle), containsObject);
        return result && containsObject ? 1 : 0;
    }

    uint8_t ScriptRuntime::has_transform_internal(
        CueEntityHandle a_entityHandle) const noexcept
    {
        if (m_gameWorld == nullptr || a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return 0;
        }

        bool hasTransform = false;
        const Result result = m_gameWorld->has_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), hasTransform);
        return result && hasTransform ? 1 : 0;
    }

    CueResult ScriptRuntime::get_transform_internal(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform) const noexcept
    {
        if (a_outTransform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld->get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        a_outTransform->position =
            { transform->position.x, transform->position.y, transform->position.z };
        const Math::float3 rotationRadians =
            Math::quaternion_to_euler_xyz(transform->rotation);
        a_outTransform->rotation =
            { rotationRadians.x, rotationRadians.y, rotationRadians.z };
        a_outTransform->scale =
            { transform->scale.x, transform->scale.y, transform->scale.z };
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_transform_internal(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform) noexcept
    {
        if (a_transform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld->get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        transform->position =
            Math::float3(a_transform->position.x, a_transform->position.y,
                a_transform->position.z);
        transform->rotation = Math::quaternion_from_euler_xyz(
            Math::float3(
                a_transform->rotation.x,
                a_transform->rotation.y,
                a_transform->rotation.z));
        transform->scale =
            Math::float3(a_transform->scale.x, a_transform->scale.y,
                a_transform->scale.z);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::get_transform_degrees_internal(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform) const noexcept
    {
        const CueResult result =
            get_transform_internal(a_entityHandle, a_outTransform);
        if (result != CueResult_Ok)
        {
            return result;
        }

        const Math::float3 rotationRadians(
            a_outTransform->rotation.x,
            a_outTransform->rotation.y,
            a_outTransform->rotation.z);
        const Math::float3 rotationDegrees =
            Math::radians_to_degrees(rotationRadians);
        a_outTransform->rotation =
            { rotationDegrees.x, rotationDegrees.y, rotationDegrees.z };
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_transform_degrees_internal(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform) noexcept
    {
        if (a_transform == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        CueTransformData transform = *a_transform;
        const Math::float3 rotationDegrees(
            transform.rotation.x,
            transform.rotation.y,
            transform.rotation.z);
        const Math::float3 rotationRadians =
            Math::degrees_to_radians(rotationDegrees);
        transform.rotation =
            { rotationRadians.x, rotationRadians.y, rotationRadians.z };
        return set_transform_internal(a_entityHandle, &transform);
    }

    CueResult ScriptRuntime::get_transform_quaternion_internal(
        CueEntityHandle a_entityHandle,
        CueTransformQuaternionData* a_outTransform) const noexcept
    {
        if (a_outTransform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld->get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        a_outTransform->position =
            { transform->position.x, transform->position.y, transform->position.z };
        a_outTransform->rotation = {
            transform->rotation.x,
            transform->rotation.y,
            transform->rotation.z,
            transform->rotation.w
        };
        a_outTransform->scale =
            { transform->scale.x, transform->scale.y, transform->scale.z };
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_transform_quaternion_internal(
        CueEntityHandle a_entityHandle,
        const CueTransformQuaternionData* a_transform) noexcept
    {
        if (a_transform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld->get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        transform->position =
            Math::float3(a_transform->position.x, a_transform->position.y,
                a_transform->position.z);
        transform->rotation = Math::Quaternion::normalize(Math::Quaternion(
            a_transform->rotation.x,
            a_transform->rotation.y,
            a_transform->rotation.z,
            a_transform->rotation.w));
        transform->scale =
            Math::float3(a_transform->scale.x, a_transform->scale.y,
                a_transform->scale.z);
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::push_key_internal(CueKey a_key) const noexcept
    {
        if (m_platform == nullptr)
        {
            return 0;
        }

        return m_platform->input_manager().push_key(to_pal_key(a_key)) ? 1 : 0;
    }

    CueResult ScriptRuntime::request_audio_source_play_internal(
        CueEntityHandle a_entityHandle) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::AudioSourceComponent* audioSource = nullptr;
        const Result result =
            m_gameWorld->get_component<ECS::AudioSourceComponent>(
                to_entity_id(a_entityHandle),
                audioSource);
        if (!result || audioSource == nullptr)
        {
            return convert_result_code(result);
        }

        audioSource->playRequested = true;
        audioSource->stopRequested = false;
        return CueResult_Ok;
    }

    CueSceneId ScriptRuntime::request_scene_load_internal(
        CueStringView a_sceneName) noexcept
    {
        if (a_sceneName.data == nullptr || a_sceneName.size == 0)
        {
            return k_cueInvalidSceneId;
        }
        if (m_gameWorld == nullptr)
        {
            return k_cueInvalidSceneId;
        }

        const std::string_view sceneName(a_sceneName.data, a_sceneName.size);
        GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
        const Result result = m_gameWorld->request_load_scene(sceneName, sceneId);
        if (!result)
        {
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "[Script][Warning] Scene load request failed: {}", result.message);
            return k_cueInvalidSceneId;
        }

        return static_cast<CueSceneId>(sceneId);
    }

    CueResult ScriptRuntime::request_scene_unload_internal(
        CueSceneId a_sceneId) noexcept
    {
        if (a_sceneId == k_cueInvalidSceneId)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result =
            m_gameWorld->request_unload_scene(
                static_cast<GameCore::SceneId>(a_sceneId));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::set_rigid_body_linear_velocity_internal(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_velocity == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->set_rigid_body_linear_velocity(
            to_entity_id(a_entityHandle),
            Math::float3(a_velocity->x, a_velocity->y, a_velocity->z));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::get_rigid_body_linear_velocity_internal(
        CueEntityHandle a_entityHandle,
        CueFloat3* a_outVelocity) const noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_outVelocity == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        Math::float3 velocity = Math::float3::zero();
        const Result result = m_gameWorld->get_rigid_body_linear_velocity(
            to_entity_id(a_entityHandle), velocity);
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outVelocity = CueFloat3{ velocity.x, velocity.y, velocity.z };
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::add_rigid_body_force_internal(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_force) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_force == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->add_rigid_body_force(
            to_entity_id(a_entityHandle),
            Math::float3(a_force->x, a_force->y, a_force->z));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::add_rigid_body_impulse_internal(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_impulse) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_impulse == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->add_rigid_body_impulse(
            to_entity_id(a_entityHandle),
            Math::float3(a_impulse->x, a_impulse->y, a_impulse->z));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::set_character_move_velocity_internal(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_velocity == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->set_character_move_velocity(
            to_entity_id(a_entityHandle),
            Math::float3(a_velocity->x, a_velocity->y, a_velocity->z));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::request_character_jump_internal(
        CueEntityHandle a_entityHandle) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result =
            m_gameWorld->request_character_jump(to_entity_id(a_entityHandle));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::set_nav_agent_destination_internal(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_destination) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_destination == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->set_nav_agent_destination(
            to_entity_id(a_entityHandle),
            Math::float3(a_destination->x, a_destination->y, a_destination->z));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::set_nav_agent_target_internal(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_targetEntityHandle) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_targetEntityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->set_nav_agent_target(
            to_entity_id(a_entityHandle),
            to_entity_id(a_targetEntityHandle));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::get_mouse_delta_internal(
        CueMouseDeltaData* a_outDelta) const noexcept
    {
        if (a_outDelta == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outDelta = {};
        if (m_platform == nullptr)
        {
            return CueResult_InvalidState;
        }

        const PAL::MouseDelta delta =
            m_platform->input_manager().mouse_delta();
        *a_outDelta = CueMouseDeltaData{ delta.x, delta.y, delta.wheel };
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::push_mouse_button_internal(
        CueMouseButton a_button) const noexcept
    {
        if (m_platform == nullptr)
        {
            return 0;
        }

        const PAL::MouseButton button = to_pal_mouse_button(a_button);
        if (button == PAL::MouseButton::Count)
        {
            return 0;
        }

        return m_platform->input_manager().push_mouse_button(button) ? 1 : 0;
    }

    CueResult ScriptRuntime::raycast_internal(
        const CueRaycastDesc* a_desc,
        CueRaycastHit* a_outHit) const noexcept
    {
        if (a_desc == nullptr || a_outHit == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outHit = {};
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        GameCore::GameWorld::GameplayRaycastDesc desc{};
        desc.origin =
            Math::float3(a_desc->origin.x, a_desc->origin.y, a_desc->origin.z);
        desc.direction = Math::float3(
            a_desc->direction.x, a_desc->direction.y, a_desc->direction.z);
        desc.ignoredEntity = to_entity_id(a_desc->ignoredEntity);
        desc.distance = a_desc->distance;

        GameCore::GameWorld::GameplayRaycastHit hit{};
        const Result result = m_gameWorld->raycast(desc, hit);
        if (!result)
        {
            return convert_result_code(result);
        }

        a_outHit->entity = to_entity_handle(hit.entity);
        a_outHit->position =
            CueFloat3{ hit.position.x, hit.position.y, hit.position.z };
        a_outHit->normal =
            CueFloat3{ hit.normal.x, hit.normal.y, hit.normal.z };
        a_outHit->distance = hit.distance;
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::debug_draw_line_internal(
        const CueFloat3* a_start,
        const CueFloat3* a_end,
        const CueFloat4* a_color,
        float a_durationSeconds) noexcept
    {
        if (a_start == nullptr || a_end == nullptr || a_color == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        m_gameWorld->debug_draw().add_line(
            Math::float3(a_start->x, a_start->y, a_start->z),
            Math::float3(a_end->x, a_end->y, a_end->z),
            Math::float4(a_color->x, a_color->y, a_color->z, a_color->w),
            a_durationSeconds);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::debug_draw_sphere_internal(
        const CueFloat3* a_center,
        float a_radius,
        const CueFloat4* a_color,
        float a_durationSeconds) noexcept
    {
        if (a_center == nullptr || a_color == nullptr || a_radius <= 0.0f)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        m_gameWorld->debug_draw().add_sphere(
            Math::float3(a_center->x, a_center->y, a_center->z),
            a_radius,
            Math::float4(a_color->x, a_color->y, a_color->z, a_color->w),
            a_durationSeconds);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::debug_draw_box_internal(
        const CueFloat3* a_center,
        const CueFloat3* a_halfExtent,
        const CueFloat4* a_color,
        float a_durationSeconds) noexcept
    {
        if (a_center == nullptr || a_halfExtent == nullptr || a_color == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        m_gameWorld->debug_draw().add_box(
            Math::float3(a_center->x, a_center->y, a_center->z),
            Math::float3(a_halfExtent->x, a_halfExtent->y, a_halfExtent->z),
            Math::float4(a_color->x, a_color->y, a_color->z, a_color->w),
            a_durationSeconds);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::spawn_object_internal(
        const CueSpawnObjectDesc* a_desc,
        CueEntityHandle* a_outEntityHandle) noexcept
    {
        if (a_desc == nullptr || a_outEntityHandle == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outEntityHandle = CueEntityHandle{ k_cueInvalidHandleValue };
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Math::float3 position = to_math_float3(a_desc->transform.position);
        GameCore::GameObject object{};
        Result result{};
        GameCore::SceneId sceneId =
            static_cast<GameCore::SceneId>(a_desc->sceneId);
        if (sceneId == GameCore::k_invalidSceneId &&
            m_executingEntityId != GameCore::k_invalidEntityId)
        {
            GameCore::SceneId executingSceneId = GameCore::k_invalidSceneId;
            const Result sceneResult =
                m_gameWorld->source_scene_id(m_executingEntityId, executingSceneId);
            if (sceneResult && executingSceneId != GameCore::k_invalidSceneId)
            {
                sceneId = executingSceneId;
            }
        }
        const bool isSceneObject = sceneId != GameCore::k_invalidSceneId;

        switch (a_desc->kind)
        {
        case CueSpawnObjectKind_Empty:
            result = isSceneObject
                ? m_gameWorld->add_game_object_to_scene(sceneId, position, object)
                : m_gameWorld->add_game_object(position, object);
            break;
        case CueSpawnObjectKind_StaticMesh:
            result = isSceneObject
                ? m_gameWorld->add_object_to_scene(sceneId, position, object)
                : m_gameWorld->add_object(position, object);
            break;
        case CueSpawnObjectKind_Sprite:
            result = isSceneObject
                ? m_gameWorld->add_sprite_object_to_scene(sceneId, position, object)
                : m_gameWorld->add_sprite_object(position, object);
            break;
        case CueSpawnObjectKind_Camera:
            result = isSceneObject
                ? m_gameWorld->add_camera_object_to_scene(sceneId, position, object)
                : m_gameWorld->add_camera_object(position, object);
            break;
        case CueSpawnObjectKind_DirectionalLight:
            result = isSceneObject
                ? m_gameWorld->add_directional_light_object_to_scene(
                    sceneId, position, object)
                : m_gameWorld->add_directional_light_object(position, object);
            break;
        case CueSpawnObjectKind_PointLight:
            result = isSceneObject
                ? m_gameWorld->add_point_light_object_to_scene(
                    sceneId, position, object)
                : m_gameWorld->add_point_light_object(position, object);
            break;
        case CueSpawnObjectKind_SpotLight:
            result = isSceneObject
                ? m_gameWorld->add_spot_light_object_to_scene(
                    sceneId, position, object)
                : m_gameWorld->add_spot_light_object(position, object);
            break;
        default:
            return CueResult_InvalidArgument;
        }

        if (!result || !object.is_valid())
        {
            return convert_result_code(result);
        }

        const auto cleanupOnFailure = [&object]() noexcept
        {
            if (object.is_valid())
            {
                (void)object.destroy();
            }
        };

        if (has_text(a_desc->name))
        {
            result = object.set_name(to_string_view(a_desc->name));
            if (!result)
            {
                cleanupOnFailure();
                return convert_result_code(result);
            }
        }

        if (has_text(a_desc->tag))
        {
            result = object.set_tag(to_string_view(a_desc->tag));
            if (!result)
            {
                cleanupOnFailure();
                return convert_result_code(result);
            }
        }

        result = object.set_active(a_desc->isActive != 0u);
        if (!result)
        {
            cleanupOnFailure();
            return convert_result_code(result);
        }

        result = object.set_persistent(a_desc->isPersistent != 0u);
        if (!result)
        {
            cleanupOnFailure();
            return convert_result_code(result);
        }

        CueEntityHandle entityHandle = to_entity_handle(object.entity_id());
        const CueResult transformResult =
            set_transform_internal(entityHandle, &a_desc->transform);
        if (transformResult != CueResult_Ok)
        {
            cleanupOnFailure();
            return transformResult;
        }

        *a_outEntityHandle = entityHandle;
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::instantiate_entity_internal(
        const CueInstantiateEntityDesc* a_desc,
        CueEntityHandle* a_outEntityHandle) noexcept
    {
        if (a_desc == nullptr || a_outEntityHandle == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outEntityHandle = CueEntityHandle{ k_cueInvalidHandleValue };
        if (a_desc->sourceEntity.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        GameCore::DeletedObjectSnapshot snapshot{};
        Result result = m_gameWorld->capture_deleted_object(
            to_entity_id(a_desc->sourceEntity), snapshot);
        if (!result)
        {
            return convert_result_code(result);
        }

        snapshot.definition.localObjectId = GameCore::k_invalidLocalObjectId;
        snapshot.definition.parentLocalObjectId.reset();

        GameCore::EntityId entityId = GameCore::k_invalidEntityId;
        result = m_gameWorld->restore_deleted_object(snapshot, entityId);
        if (!result || entityId == GameCore::k_invalidEntityId)
        {
            return convert_result_code(result);
        }

        const auto cleanupOnFailure = [this, entityId]() noexcept
        {
            if (m_gameWorld != nullptr)
            {
                (void)m_gameWorld->destroy_object(entityId);
            }
        };

        if (a_desc->usesName != 0u)
        {
            result =
                m_gameWorld->set_object_name(entityId, to_string_view(a_desc->name));
            if (!result)
            {
                cleanupOnFailure();
                return convert_result_code(result);
            }
        }

        if (a_desc->usesTag != 0u)
        {
            result =
                m_gameWorld->set_object_tag(entityId, to_string_view(a_desc->tag));
            if (!result)
            {
                cleanupOnFailure();
                return convert_result_code(result);
            }
        }

        if (a_desc->usesActive != 0u)
        {
            result =
                m_gameWorld->set_object_active(entityId, a_desc->isActive != 0u);
            if (!result)
            {
                cleanupOnFailure();
                return convert_result_code(result);
            }
        }

        CueEntityHandle entityHandle = to_entity_handle(entityId);
        if (a_desc->usesTransform != 0u)
        {
            const CueResult transformResult =
                set_transform_internal(entityHandle, &a_desc->transform);
            if (transformResult != CueResult_Ok)
            {
                cleanupOnFailure();
                return transformResult;
            }
        }

        *a_outEntityHandle = entityHandle;
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::find_entities_by_tag_internal(
        CueStringView a_tag,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount) noexcept
    {
        if (a_outCount == nullptr ||
            (a_capacity > 0u && a_outEntityHandles == nullptr))
        {
            return CueResult_InvalidArgument;
        }
        *a_outCount = 0u;
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        std::vector<GameCore::GameObject> objects{};
        const Result result =
            m_gameWorld->find_objects_by_tag(to_string_view(a_tag), objects);
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outCount = static_cast<uint32_t>(objects.size());
        const uint32_t writeCount =
            (std::min)(a_capacity, static_cast<uint32_t>(objects.size()));
        for (uint32_t index = 0u; index < writeCount; ++index)
        {
            a_outEntityHandles[index] =
                to_entity_handle(objects[index].entity_id());
        }

        return CueResult_Ok;
    }

    CueResult ScriptRuntime::find_entities_by_name_internal(
        CueStringView a_name,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount) noexcept
    {
        if (a_outCount == nullptr ||
            (a_capacity > 0u && a_outEntityHandles == nullptr))
        {
            return CueResult_InvalidArgument;
        }
        *a_outCount = 0u;
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        std::vector<GameCore::GameObject> objects{};
        const Result result =
            m_gameWorld->find_objects_by_name(to_string_view(a_name), objects);
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outCount = static_cast<uint32_t>(objects.size());
        const uint32_t writeCount =
            (std::min)(a_capacity, static_cast<uint32_t>(objects.size()));
        for (uint32_t index = 0u; index < writeCount; ++index)
        {
            a_outEntityHandles[index] =
                to_entity_handle(objects[index].entity_id());
        }

        return CueResult_Ok;
    }

    CueResult ScriptRuntime::trigger_overlaps_internal(
        CueEntityHandle a_triggerEntity,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount) noexcept
    {
        if (a_outCount == nullptr ||
            (a_capacity > 0u && a_outEntityHandles == nullptr))
        {
            return CueResult_InvalidArgument;
        }
        *a_outCount = 0u;
        if (a_triggerEntity.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        std::vector<GameCore::EntityId> entities{};
        const Result result =
            m_gameWorld->trigger_overlaps(to_entity_id(a_triggerEntity), entities);
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outCount = static_cast<uint32_t>(entities.size());
        const uint32_t writeCount =
            (std::min)(a_capacity, static_cast<uint32_t>(entities.size()));
        for (uint32_t index = 0u; index < writeCount; ++index)
        {
            a_outEntityHandles[index] = to_entity_handle(entities[index]);
        }

        return CueResult_Ok;
    }

    CueResult ScriptRuntime::sphere_overlap_internal(
        const CueSphereOverlapDesc* a_desc,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount) noexcept
    {
        if (a_desc == nullptr || a_outCount == nullptr ||
            (a_capacity > 0u && a_outEntityHandles == nullptr))
        {
            return CueResult_InvalidArgument;
        }
        *a_outCount = 0u;
        if (a_desc->radius <= 0.0f)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Math::float3 center = to_math_float3(a_desc->center);
        const GameCore::EntityId ignoredEntity = to_entity_id(a_desc->ignoredEntity);
        std::vector<GameCore::EntityId> entities{};

        const Result result = m_gameWorld->for_each_object(
            [this, &entities, center, ignoredEntity, a_desc](
                GameCore::EntityId a_entityId,
                GameCore::SceneId,
                GameCore::GameObject)
            {
                if (a_entityId == ignoredEntity)
                {
                    return;
                }

                ECS::TransformComponent* transform = nullptr;
                ECS::ColliderComponent* collider = nullptr;
                if (!m_gameWorld->get_component<ECS::TransformComponent>(
                        a_entityId, transform) ||
                    !m_gameWorld->get_component<ECS::ColliderComponent>(
                        a_entityId, collider) ||
                    transform == nullptr ||
                    collider == nullptr)
                {
                    return;
                }
                if (a_desc->includeTriggers == 0u && collider->isTrigger)
                {
                    return;
                }

                const Math::float3 candidateCenter =
                    transform->position + collider->offset;
                const float combinedRadius =
                    a_desc->radius + collider_bounding_radius(*collider);
                if (length_squared(candidateCenter - center) <=
                    combinedRadius * combinedRadius)
                {
                    entities.push_back(a_entityId);
                }
            });
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outCount = static_cast<uint32_t>(entities.size());
        const uint32_t writeCount =
            (std::min)(a_capacity, static_cast<uint32_t>(entities.size()));
        for (uint32_t index = 0u; index < writeCount; ++index)
        {
            a_outEntityHandles[index] = to_entity_handle(entities[index]);
        }

        return CueResult_Ok;
    }

    CueResult ScriptRuntime::destroy_entity_internal(
        CueEntityHandle a_entityHandle) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result =
            m_gameWorld->destroy_object(to_entity_id(a_entityHandle));
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::get_camera_fov_y_internal(
        CueEntityHandle a_entityHandle,
        float* a_outFovY) const noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_outFovY == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outFovY = 0.0f;
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::CameraComponent* camera = nullptr;
        const Result result =
            m_gameWorld->get_component(to_entity_id(a_entityHandle), camera);
        if (!result || camera == nullptr)
        {
            return convert_result_code(result);
        }

        *a_outFovY = camera->fovY;
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_camera_fov_y_internal(
        CueEntityHandle a_entityHandle,
        float a_fovY) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            !std::isfinite(a_fovY) ||
            a_fovY <= 0.0f ||
            a_fovY >= 179.0f)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        ECS::CameraComponent* camera = nullptr;
        const Result result =
            m_gameWorld->get_component(to_entity_id(a_entityHandle), camera);
        if (!result || camera == nullptr)
        {
            return convert_result_code(result);
        }

        camera->fovY = a_fovY;
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::add_or_set_component_internal(
        CueEntityHandle a_entityHandle,
        CueComponentKind a_componentKind,
        const void* a_componentData,
        uint32_t a_componentDataSize) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_componentData == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const GameCore::EntityId entityId = to_entity_id(a_entityHandle);
        bool containsObject = false;
        Result result = m_gameWorld->contains_object(entityId, containsObject);
        if (!result)
        {
            return convert_result_code(result);
        }
        if (!containsObject)
        {
            return CueResult_NotFound;
        }

        const auto addOrSetComponent =
            [this, entityId](const auto& a_component) -> CueResult
        {
            using Component = std::decay_t<decltype(a_component)>;

            Component* target = nullptr;
            Result getResult = m_gameWorld->get_component(entityId, target);
            if (getResult && target != nullptr)
            {
                *target = a_component;
                return CueResult_Ok;
            }

            Result addResult =
                m_gameWorld->add_component<Component>(entityId, target);
            if (!addResult || target == nullptr)
            {
                return convert_result_code(addResult);
            }

            *target = a_component;
            return CueResult_Ok;
        };

        switch (a_componentKind)
        {
        case CueComponentKind_Camera:
        {
            if (a_componentDataSize < sizeof(CueCameraComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueCameraComponentData*>(a_componentData);
            if (!std::isfinite(data.fovY) || data.fovY <= 0.0f ||
                data.fovY >= 179.0f ||
                !std::isfinite(data.aspectRatio) ||
                data.aspectRatio <= 0.0f ||
                !std::isfinite(data.nearZ) ||
                !std::isfinite(data.farZ) ||
                data.nearZ <= 0.0f ||
                data.farZ <= data.nearZ)
            {
                return CueResult_InvalidArgument;
            }

            ECS::CameraComponent camera{};
            camera.fovY = data.fovY;
            camera.aspectRatio = data.aspectRatio;
            camera.nearZ = data.nearZ;
            camera.farZ = data.farZ;
            camera.isMain = data.isMain != 0u;
            return addOrSetComponent(camera);
        }
        case CueComponentKind_Collider:
        {
            if (a_componentDataSize < sizeof(CueColliderComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueColliderComponentData*>(a_componentData);

            Physics::ShapeType shapeType{};
            if (!to_physics_shape_type(data.shapeType, shapeType) ||
                !std::isfinite(data.radius) ||
                !std::isfinite(data.halfHeight) ||
                !std::isfinite(data.friction) ||
                !std::isfinite(data.restitution) ||
                data.radius <= 0.0f ||
                data.halfHeight <= 0.0f)
            {
                return CueResult_InvalidArgument;
            }

            ECS::ColliderComponent collider{};
            collider.type = shapeType;
            collider.meshModelName = std::string(to_string_view(data.meshModelName));
            collider.offset = to_math_float3(data.offset);
            collider.halfExtent = to_math_float3(data.halfExtent);
            collider.radius = data.radius;
            collider.halfHeight = data.halfHeight;
            collider.friction = data.friction;
            collider.restitution = data.restitution;
            collider.layer = data.layer;
            collider.mask = data.mask;
            collider.isTrigger = data.isTrigger != 0u;
            return addOrSetComponent(collider);
        }
        case CueComponentKind_TriggerVolume:
        {
            if (a_componentDataSize < sizeof(CueTriggerVolumeComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueTriggerVolumeComponentData*>(
                    a_componentData);

            ECS::TriggerVolumeComponent trigger{};
            trigger.includeTriggers = data.includeTriggers != 0u;
            return addOrSetComponent(trigger);
        }
        case CueComponentKind_MeshFilter:
        {
            if (a_componentDataSize < sizeof(CueMeshFilterComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueMeshFilterComponentData*>(a_componentData);

            ECS::MeshFilterComponent meshFilter{};
            meshFilter.modelName = std::string(to_string_view(data.modelName));
            meshFilter.meshId = data.meshId;
            return addOrSetComponent(meshFilter);
        }
        case CueComponentKind_StaticMeshRenderer:
        {
            if (a_componentDataSize < sizeof(CueStaticMeshRendererComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueStaticMeshRendererComponentData*>(
                    a_componentData);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.visible = data.visible != 0u;
            renderer.castsShadow = data.castsShadow != 0u;
            renderer.receivesShadow = data.receivesShadow != 0u;
            renderer.shadowCasterMode =
                data.shadowCasterMode == CueShadowCasterMode_TwoSided
                    ? ECS::ShadowCasterMode::TwoSided
                    : ECS::ShadowCasterMode::Solid;
            return addOrSetComponent(renderer);
        }
        case CueComponentKind_SpriteRenderer:
        {
            if (a_componentDataSize < sizeof(CueSpriteRendererComponentData))
            {
                return CueResult_InvalidArgument;
            }
            const auto& data =
                *static_cast<const CueSpriteRendererComponentData*>(
                    a_componentData);

            ECS::SpriteRendererComponent renderer{};
            renderer.color = Math::float4(
                data.color.x, data.color.y, data.color.z, data.color.w);
            renderer.uvRect = Math::float4(
                data.uvRect.x, data.uvRect.y, data.uvRect.z, data.uvRect.w);
            renderer.size = to_math_float2(data.size);
            renderer.pivot = to_math_float2(data.pivot);
            renderer.layer = data.layer;
            renderer.order = data.order;
            renderer.isVisible = data.isVisible != 0u;
            return addOrSetComponent(renderer);
        }
        default:
            return CueResult_InvalidArgument;
        }
    }

    CueResult ScriptRuntime::get_parent_internal(
        CueEntityHandle a_entityHandle,
        CueEntityHandle* a_outParentEntity) const noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_outParentEntity == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        *a_outParentEntity = CueEntityHandle{ k_cueInvalidHandleValue };
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        GameCore::EntityId parentEntity = GameCore::k_invalidEntityId;
        const Result result =
            m_gameWorld->get_parent(to_entity_id(a_entityHandle), parentEntity);
        if (!result)
        {
            return convert_result_code(result);
        }

        *a_outParentEntity = to_entity_handle(parentEntity);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_parent_internal(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_parentEntity,
        uint8_t a_keepsWorldTransform) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_parentEntity.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->set_parent(
            to_entity_id(a_entityHandle),
            to_entity_id(a_parentEntity),
            a_keepsWorldTransform != 0u);
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::detach_parent_internal(
        CueEntityHandle a_entityHandle,
        uint8_t a_keepsWorldTransform) noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }
        if (m_gameWorld == nullptr)
        {
            return CueResult_InvalidState;
        }

        const Result result = m_gameWorld->detach_parent(
            to_entity_id(a_entityHandle),
            a_keepsWorldTransform != 0u);
        return convert_result_code(result);
    }

    CueResult ScriptRuntime::find_script_instance_internal(
        CueEntityHandle a_entityHandle,
        CueStringView a_scriptClassName,
        CueScriptInstanceHandle* a_outInstanceHandle) const noexcept
    {
        if (a_outInstanceHandle == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        *a_outInstanceHandle = CueScriptInstanceHandle{ k_cueInvalidHandleValue };

        if (a_entityHandle.value == k_cueInvalidHandleValue ||
            a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const auto bindingIt = m_bindings.find(to_entity_id(a_entityHandle));
        if (bindingIt == m_bindings.end())
        {
            return CueResult_NotFound;
        }

        const std::string_view className = to_string_view(a_scriptClassName);
        if (bindingIt->second.className != className)
        {
            return CueResult_NotFound;
        }

        *a_outInstanceHandle = bindingIt->second.instanceHandle;
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::is_script_instance_valid_internal(
        CueScriptInstanceHandle a_instanceHandle) const noexcept
    {
        if (a_instanceHandle.value == k_cueInvalidHandleValue)
        {
            return 0;
        }

        const auto entityIt =
            m_entityIdsByInstanceHandle.find(a_instanceHandle.value);
        if (entityIt == m_entityIdsByInstanceHandle.end())
        {
            return 0;
        }

        const auto bindingIt = m_bindings.find(entityIt->second);
        return bindingIt != m_bindings.end() &&
                bindingIt->second.instanceHandle.value == a_instanceHandle.value
            ? 1
            : 0;
    }

    CueResult ScriptRuntime::get_script_field_internal(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue) const noexcept
    {
        if (a_outFieldValue == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        *a_outFieldValue = {};

        if (a_instanceHandle.value == k_cueInvalidHandleValue ||
            a_fieldName.data == nullptr ||
            a_fieldName.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const auto entityIt =
            m_entityIdsByInstanceHandle.find(a_instanceHandle.value);
        if (entityIt == m_entityIdsByInstanceHandle.end())
        {
            return CueResult_NotFound;
        }

        const auto bindingIt = m_bindings.find(entityIt->second);
        if (bindingIt == m_bindings.end() ||
            bindingIt->second.instanceHandle.value != a_instanceHandle.value)
        {
            return CueResult_NotFound;
        }

        const std::string_view fieldName = to_string_view(a_fieldName);
        const auto fieldIt =
            std::find_if(bindingIt->second.fieldValues.begin(),
                bindingIt->second.fieldValues.end(),
                [&fieldName](const ECS::ScriptFieldValue& a_fieldValue)
                {
                    return a_fieldValue.name == fieldName;
                });
        if (fieldIt == bindingIt->second.fieldValues.end())
        {
            return CueResult_NotFound;
        }

        *a_outFieldValue = to_cue_script_field_value(*fieldIt);
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::is_script_class_registered_internal(
        CueStringView a_scriptClassName) const noexcept
    {
        if (a_scriptClassName.data == nullptr || a_scriptClassName.size == 0)
        {
            return 0;
        }

        return has_registered_script_class(to_string_view(a_scriptClassName)) ? 1 : 0;
    }

    CueResult ScriptRuntime::get_script_class_field_internal(
        CueStringView a_scriptClassName,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue) const noexcept
    {
        if (a_outFieldValue == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        *a_outFieldValue = {};

        if (a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0 ||
            a_fieldName.data == nullptr ||
            a_fieldName.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const auto infoIt =
            m_scriptClassInfos.find(std::string(to_string_view(a_scriptClassName)));
        if (infoIt == m_scriptClassInfos.end())
        {
            return CueResult_NotFound;
        }

        const std::string_view fieldName = to_string_view(a_fieldName);
        const auto fieldIt =
            std::find_if(infoIt->second.fieldDefaults.begin(),
                infoIt->second.fieldDefaults.end(),
                [&fieldName](const ECS::ScriptFieldValue& a_fieldValue)
                {
                    return a_fieldValue.name == fieldName;
                });
        if (fieldIt == infoIt->second.fieldDefaults.end())
        {
            return CueResult_NotFound;
        }

        *a_outFieldValue = to_cue_script_field_value(*fieldIt);
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::has_script_class_function_internal(
        CueStringView a_scriptClassName,
        CueStringView a_functionName) const noexcept
    {
        if (a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0 ||
            a_functionName.data == nullptr ||
            a_functionName.size == 0)
        {
            return 0;
        }

        const auto infoIt =
            m_scriptClassInfos.find(std::string(to_string_view(a_scriptClassName)));
        if (infoIt == m_scriptClassInfos.end())
        {
            return 0;
        }

        return infoIt->second.marionnetteClass.find_function(
                   to_string_view(a_functionName)) != nullptr
            ? 1
            : 0;
    }

    CueResult ScriptRuntime::invoke_script_function_internal(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_functionName) const noexcept
    {
        if (a_instanceHandle.value == k_cueInvalidHandleValue ||
            a_functionName.data == nullptr ||
            a_functionName.size == 0)
        {
            return CueResult_InvalidArgument;
        }
        if (!ScriptModule::is_loaded(m_module))
        {
            return CueResult_InvalidState;
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr ||
            exports->structSize <
                offsetof(CueScriptExports, invokeScriptFunction) +
                    sizeof(CueInvokeScriptFunctionFn) ||
            exports->invokeScriptFunction == nullptr)
        {
            return CueResult_Unsupported;
        }

        return exports->invokeScriptFunction(a_instanceHandle, a_functionName);
    }

    MarionnetteObject* ScriptRuntime::get_script_object_internal(
        CueScriptInstanceHandle a_instanceHandle) const noexcept
    {
        if (a_instanceHandle.value == k_cueInvalidHandleValue)
        {
            return nullptr;
        }

        const auto entityIt =
            m_entityIdsByInstanceHandle.find(a_instanceHandle.value);
        if (entityIt == m_entityIdsByInstanceHandle.end())
        {
            return nullptr;
        }

        const auto bindingIt = m_bindings.find(entityIt->second);
        if (bindingIt == m_bindings.end() ||
            bindingIt->second.instanceHandle.value != a_instanceHandle.value)
        {
            return nullptr;
        }

        return bindingIt->second.scriptObject;
    }

    Result ScriptRuntime::convert_script_result(CueResult a_result) noexcept
    {
        switch (a_result)
        {
        case CueResult_Ok:
            return Result::ok();

        case CueResult_InvalidArgument:
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script runtime returned InvalidArgument.");

        case CueResult_NotFound:
            return Result::fail(Code::NotFound, Severity::Error,
                "Script runtime returned NotFound.");

        case CueResult_Unsupported:
            return Result::fail(Code::Unsupported, Severity::Error,
                "Script runtime returned Unsupported.");

        case CueResult_InvalidState:
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime returned InvalidState.");

        case CueResult_InternalError:
            return Result::fail(Code::InternalError, Severity::Error,
                "Script runtime returned InternalError.");
        }

        return Result::fail(Code::UnknownError, Severity::Error,
            "Script runtime returned an unknown result code.");
    }

    CueResult ScriptRuntime::convert_result_code(const Result& a_result) noexcept
    {
        switch (a_result.code)
        {
        case Code::OK:
            return CueResult_Ok;

        case Code::InvalidArgument:
            return CueResult_InvalidArgument;

        case Code::NotFound:
            return CueResult_NotFound;

        case Code::Unsupported:
            return CueResult_Unsupported;

        case Code::InvalidState:
            return CueResult_InvalidState;

        case Code::InternalError:
        case Code::UnknownError:
        case Code::InitializeFailed:
        case Code::CreateFailed:
        case Code::GetFailed:
        case Code::OutOfMemory:
        case Code::AccessDenied:
        case Code::DeviceLost:
            return CueResult_InternalError;
        }

        return CueResult_InternalError;
    }

    GameCore::EntityId ScriptRuntime::to_entity_id(
        CueEntityHandle a_entityHandle) noexcept
    {
        return a_entityHandle.value != k_cueInvalidHandleValue
            ? static_cast<GameCore::EntityId>(a_entityHandle.value)
            : GameCore::k_invalidEntityId;
    }

    CueEntityHandle ScriptRuntime::to_entity_handle(
        GameCore::EntityId a_entityId) noexcept
    {
        return a_entityId != GameCore::k_invalidEntityId
            ? CueEntityHandle{ static_cast<uint64_t>(a_entityId) }
            : CueEntityHandle{ k_cueInvalidHandleValue };
    }
}
