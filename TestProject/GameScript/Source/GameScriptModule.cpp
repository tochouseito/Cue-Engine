#include <Native/ScriptAbi.h>
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace
{
    using Cue::Core::Native::ScriptClassDefinition;
    using Cue::Core::Native::ScriptFieldReader;
    using Cue::Core::Native::ScriptModuleRuntime;
    using Cue::Core::Native::make_script_class_definition;
    using Cue::Core::Native::make_script_string_view;

    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;

    struct RotateCubeStateBlob final
    {
        uint32_t version = 1u;
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

            ScriptFieldReader fieldReader(a_createInfo);
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

    const ScriptClassDefinition k_rotateCubeScript =
        make_script_class_definition<RotateCubeScript>();

    const std::array<ScriptClassDefinition, 1> k_scriptClasses = {
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
