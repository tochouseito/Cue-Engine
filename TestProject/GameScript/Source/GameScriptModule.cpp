#include <Native/ScriptAbi.h>
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <string_view>

namespace
{
    using Cue::Core::Native::ScriptClassDefinition;
    using Cue::Core::Native::ScriptModuleRuntime;
    using Cue::Core::Native::make_script_string_view;
    using Cue::Core::Native::string_view_equals;

    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;
    inline constexpr uint32_t k_stateVersion = 1u;

    struct RotateCubeState final
    {
        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;
    };

    struct RotateCubeStateBlob final
    {
        uint32_t version = k_stateVersion;
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;
    };

    inline constexpr uint32_t k_requiredEngineApiSize =
        static_cast<uint32_t>(
            offsetof(CueEngineApi, setTransform) + sizeof(CueSetTransformFn));

    [[nodiscard]] CueResult validate_engine_api(
        const CueEngineApi* a_engineApi)
    {
        if (a_engineApi == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_engineApi->structSize < k_requiredEngineApiSize)
        {
            return CueResult_InvalidArgument;
        }
        if (a_engineApi->abiVersion != k_cueScriptAbiVersion)
        {
            return CueResult_Unsupported;
        }
        if (a_engineApi->log == nullptr ||
            a_engineApi->isEntityValid == nullptr ||
            a_engineApi->hasTransform == nullptr ||
            a_engineApi->getTransform == nullptr ||
            a_engineApi->setTransform == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        return CueResult_Ok;
    }

    void log_message(const CueEngineApi* a_engineApi,
        CueLogSeverity a_severity,
        std::string_view a_message)
    {
        if (a_engineApi == nullptr || a_engineApi->log == nullptr)
        {
            return;
        }

        (void)a_engineApi->log(a_severity,
            make_script_string_view(
                a_message.data(),
                static_cast<uint32_t>(a_message.size())));
    }

    [[nodiscard]] const CueScriptFieldValue* find_field_value(
        const CueScriptCreateInfo* a_createInfo,
        CueStringView a_fieldName)
    {
        if (a_createInfo == nullptr ||
            a_createInfo->fieldValues == nullptr ||
            a_createInfo->fieldValueCount == 0)
        {
            return nullptr;
        }

        for (uint32_t fieldIndex = 0;
             fieldIndex < a_createInfo->fieldValueCount;
             ++fieldIndex)
        {
            const CueScriptFieldValue& fieldValue =
                a_createInfo->fieldValues[fieldIndex];
            if (string_view_equals(fieldValue.name, a_fieldName))
            {
                return &fieldValue;
            }
        }

        return nullptr;
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL create_rotate_cube_state(
        const CueEngineApi*,
        const CueScriptCreateInfo* a_createInfo,
        void** a_outState)
    {
        if (a_createInfo == nullptr || a_outState == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        auto* state = new (std::nothrow) RotateCubeState();
        if (state == nullptr)
        {
            return CueResult_InternalError;
        }

        state->entityHandle = a_createInfo->entityHandle;
        const CueScriptFieldValue* rotationSpeedField =
            find_field_value(
                a_createInfo, CUE_SCRIPT_STRING_VIEW("rotationSpeed"));
        if (rotationSpeedField != nullptr &&
            rotationSpeedField->type == CueScriptFieldType_Float)
        {
            state->rotationSpeed = rotationSpeedField->floatValue;
        }

        *a_outState = state;
        return CueResult_Ok;
    }

    void CUE_SCRIPT_CALL destroy_rotate_cube_state(void* a_state)
    {
        delete static_cast<RotateCubeState*>(a_state);
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL update_rotate_cube_state(
        const CueEngineApi* a_engineApi,
        void* a_state,
        float a_deltaTimeSeconds)
    {
        if (a_engineApi == nullptr || a_state == nullptr)
        {
            return CueResult_InvalidState;
        }

        auto* state = static_cast<RotateCubeState*>(a_state);
        if (a_engineApi->isEntityValid(state->entityHandle) == 0)
        {
            return CueResult_NotFound;
        }
        if (a_engineApi->hasTransform(state->entityHandle) == 0)
        {
            return CueResult_NotFound;
        }

        CueTransformData transform{};
        CueResult result =
            a_engineApi->getTransform(state->entityHandle, &transform);
        if (result != CueResult_Ok)
        {
            return result;
        }

        state->elapsedSeconds += a_deltaTimeSeconds;
        transform.rotation.y += a_deltaTimeSeconds * state->rotationSpeed;
        return a_engineApi->setTransform(state->entityHandle, &transform);
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL serialize_rotate_cube_state(
        const void* a_state,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_outStateBuffer == nullptr ||
            a_stateBufferSize != sizeof(RotateCubeStateBlob))
        {
            return CueResult_InvalidArgument;
        }

        const auto* state = static_cast<const RotateCubeState*>(a_state);
        RotateCubeStateBlob blob{};
        blob.elapsedSeconds = state->elapsedSeconds;
        blob.rotationSpeed = state->rotationSpeed;
        std::memcpy(a_outStateBuffer, &blob, sizeof(blob));
        return CueResult_Ok;
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL restore_rotate_cube_state(
        void* a_state,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_stateBuffer == nullptr ||
            a_stateBufferSize != sizeof(RotateCubeStateBlob))
        {
            return CueResult_InvalidArgument;
        }

        RotateCubeStateBlob blob{};
        std::memcpy(&blob, a_stateBuffer, sizeof(blob));
        if (blob.version != k_stateVersion)
        {
            return CueResult_Unsupported;
        }

        auto* state = static_cast<RotateCubeState*>(a_state);
        state->elapsedSeconds = blob.elapsedSeconds;
        state->rotationSpeed = blob.rotationSpeed;
        return CueResult_Ok;
    }

    inline constexpr std::array<CueScriptFieldValue, 1> k_rotateCubeFields = {
        CUE_FIELD_FLOAT("rotationSpeed", k_rotationSpeedRadiansPerSecond)
    };

    inline constexpr ScriptClassDefinition k_rotateCubeScript = CUE_SCRIPT(
        "RotateCube",
        k_stateVersion,
        RotateCubeStateBlob,
        "RotateCube:v1:elapsedSeconds:f32;rotationSpeed:f32",
        k_rotateCubeFields,
        &create_rotate_cube_state,
        &destroy_rotate_cube_state,
        &update_rotate_cube_state,
        &serialize_rotate_cube_state,
        &restore_rotate_cube_state);

    inline constexpr std::array<ScriptClassDefinition, 1> k_scriptClasses = {
        k_rotateCubeScript
    };

    [[nodiscard]] std::span<const ScriptClassDefinition> script_classes() noexcept
    {
        return std::span<const ScriptClassDefinition>(
            k_scriptClasses.data(), k_scriptClasses.size());
    }

    ScriptModuleRuntime g_scriptRuntime{};
}

extern "C"
{
    CueScriptAbiVersion CUE_SCRIPT_CALL cue_script_get_abi_version(void)
    {
        return k_cueScriptAbiVersion;
    }

    CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports)
    {
        if (a_outExports == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        a_outExports->structSize = sizeof(CueScriptExports);
        a_outExports->abiVersion = k_cueScriptAbiVersion;
        a_outExports->registerScripts =
            [](const CueEngineApi* a_engineApi) -> CueResult
            {
                const CueResult result = validate_engine_api(a_engineApi);
                if (result != CueResult_Ok)
                {
                    return result;
                }

                const CueResult registerResult =
                    g_scriptRuntime.register_scripts(
                        a_engineApi, script_classes());
                if (registerResult != CueResult_Ok)
                {
                    return registerResult;
                }

                log_message(a_engineApi, CueLogSeverity_Info,
                    "GameScript module registered.");
                return CueResult_Ok;
            };
        a_outExports->createScriptInstance =
            [](const CueScriptCreateInfo* a_createInfo,
                CueScriptInstanceHandle* a_outInstanceHandle) -> CueResult
            {
                return g_scriptRuntime.create_script_instance(
                    script_classes(), a_createInfo, a_outInstanceHandle);
            };
        a_outExports->destroyScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle) -> CueResult
            {
                return g_scriptRuntime.destroy_script_instance(a_instanceHandle);
            };
        a_outExports->updateScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                float a_deltaTimeSeconds) -> CueResult
            {
                return g_scriptRuntime.update_script_instance(
                    a_instanceHandle, a_deltaTimeSeconds);
            };
        a_outExports->getScriptInstanceStateSize =
            [](CueScriptInstanceHandle a_instanceHandle,
                uint32_t* a_outStateSize) -> CueResult
            {
                return g_scriptRuntime.get_script_instance_state_size(
                    a_instanceHandle, a_outStateSize);
            };
        a_outExports->serializeScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                void* a_outStateBuffer,
                uint32_t a_stateBufferSize) -> CueResult
            {
                return g_scriptRuntime.serialize_script_instance(
                    a_instanceHandle, a_outStateBuffer, a_stateBufferSize);
            };
        a_outExports->restoreScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                const void* a_stateBuffer,
                uint32_t a_stateBufferSize) -> CueResult
            {
                return g_scriptRuntime.restore_script_instance(
                    a_instanceHandle, a_stateBuffer, a_stateBufferSize);
            };
        a_outExports->getScriptStateDescriptor =
            [](CueStringView a_scriptClassName,
                CueScriptStateDescriptor* a_outDescriptor) -> CueResult
            {
                return g_scriptRuntime.get_script_state_descriptor(
                    script_classes(), a_scriptClassName, a_outDescriptor);
            };
        return CueResult_Ok;
    }
}
