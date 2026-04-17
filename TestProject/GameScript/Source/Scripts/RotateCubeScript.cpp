#include "RotateCubeScript.h"

#include <Native/ScriptAbi.h>

// === C++ includes ===
#include <array>
#include <span>
#include <string_view>

namespace
{
    using Cue::Core::Native::make_script_class_definition;

    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;

    struct RotateCubeStateBlob final
    {
        uint32_t version = 1u;
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;
    };

    struct RotateCubeScript final
    {
        using StateBlob = RotateCubeStateBlob;

        static constexpr std::string_view k_className = "RotateCube";
        static constexpr uint32_t k_stateVersion = 1u;
        static constexpr std::string_view k_stateSchema =
            "RotateCube:v1:elapsedSeconds:f32;rotationSpeed:f32";

        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;

        [[nodiscard]] static std::span<const CueScriptFieldValue> script_fields() noexcept
        {
            static constexpr std::array<CueScriptFieldValue, 1> k_fields = {
                CUE_FIELD_FLOAT("rotationSpeed", k_rotationSpeedRadiansPerSecond)
            };

            return std::span<const CueScriptFieldValue>(
                k_fields.data(),
                k_fields.size());
        }

        [[nodiscard]] static CueResult create(
            const CueScriptCreateInfo* a_createInfo,
            RotateCubeScript& a_state)
        {
            if (a_createInfo == nullptr)
            {
                return CueResult_InvalidArgument;
            }

            Cue::Core::Native::ScriptFieldReader fieldReader(a_createInfo);
            a_state.entityHandle = fieldReader.entity_handle();
            (void)fieldReader.read_float(
                CUE_SCRIPT_STRING_VIEW("rotationSpeed"),
                a_state.rotationSpeed);
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult update(
            const CueEngineApi* a_engineApi,
            float a_deltaTimeSeconds)
        {
            if (a_engineApi == nullptr)
            {
                return CueResult_InvalidState;
            }
            if (a_engineApi->isEntityValid(entityHandle) == 0)
            {
                return CueResult_NotFound;
            }
            if (a_engineApi->hasTransform(entityHandle) == 0)
            {
                return CueResult_NotFound;
            }

            CueTransformData transform{};
            const CueResult result =
                a_engineApi->getTransform(entityHandle, &transform);
            if (result != CueResult_Ok)
            {
                return result;
            }

            elapsedSeconds += a_deltaTimeSeconds;
            transform.rotation.y += a_deltaTimeSeconds * rotationSpeed;
            return a_engineApi->setTransform(entityHandle, &transform);
        }

        [[nodiscard]] CueResult serialize(StateBlob& a_outState) const
        {
            a_outState.version = k_stateVersion;
            a_outState.elapsedSeconds = elapsedSeconds;
            a_outState.rotationSpeed = rotationSpeed;
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult restore(const StateBlob& a_state)
        {
            if (a_state.version != k_stateVersion)
            {
                return CueResult_Unsupported;
            }

            elapsedSeconds = a_state.elapsedSeconds;
            rotationSpeed = a_state.rotationSpeed;
            return CueResult_Ok;
        }
    };
}

Cue::Core::Native::ScriptClassDefinition make_rotate_cube_script_definition() noexcept
{
    return make_script_class_definition<RotateCubeScript>();
}
