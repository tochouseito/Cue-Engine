#include <Native/ScriptAbi.h>

// === C++ includes ===
#include <cstddef>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace
{
    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;

    struct ScriptInstance final
    {
        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
        float elapsedSeconds = 0.0f;
    };

    const CueEngineApi* g_engineApi = nullptr;
    std::unordered_map<uint64_t, ScriptInstance> g_instances{};
    uint64_t g_nextInstanceId = 1;

    inline constexpr uint32_t k_requiredEngineApiSize =
        static_cast<uint32_t>(
            offsetof(CueEngineApi, setTransform) + sizeof(CueSetTransformFn));

    [[nodiscard]] bool supports_register_script_class(
        const CueEngineApi* a_engineApi)
    {
        return a_engineApi != nullptr &&
            a_engineApi->structSize >=
            offsetof(CueEngineApi, registerScriptClass) +
                sizeof(CueRegisterScriptClassFn) &&
            a_engineApi->registerScriptClass != nullptr;
    }

    [[nodiscard]] CueResult validate_engine_api(const CueEngineApi* a_engineApi)
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

    [[nodiscard]] CueStringView make_string_view(std::string_view a_text)
    {
        return CueStringView{
            a_text.data(),
            static_cast<uint32_t>(a_text.size())
        };
    }

    void log_message(CueLogSeverity a_severity, std::string_view a_message)
    {
        if (g_engineApi == nullptr || g_engineApi->log == nullptr)
        {
            return;
        }

        (void)g_engineApi->log(a_severity, make_string_view(a_message));
    }

    [[nodiscard]] bool string_view_equals(
        CueStringView a_left, std::string_view a_right)
    {
        if (a_left.data == nullptr)
        {
            return false;
        }
        if (a_left.size != a_right.size())
        {
            return false;
        }

        return std::memcmp(a_left.data, a_right.data(), a_right.size()) == 0;
    }
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

                g_engineApi = a_engineApi;
                if (supports_register_script_class(g_engineApi))
                {
                    const CueResult registerResult =
                        g_engineApi->registerScriptClass(
                            make_string_view("RotateCube"));
                    if (registerResult != CueResult_Ok)
                    {
                        return registerResult;
                    }
                }

                log_message(CueLogSeverity_Info,
                    "GameScript module registered.");
                return CueResult_Ok;
            };
        a_outExports->createScriptInstance =
            [](const CueScriptCreateInfo* a_createInfo,
                CueScriptInstanceHandle* a_outInstanceHandle) -> CueResult
            {
                if (g_engineApi == nullptr)
                {
                    return CueResult_InvalidState;
                }
                if (a_createInfo == nullptr || a_outInstanceHandle == nullptr)
                {
                    return CueResult_InvalidArgument;
                }
                if (a_createInfo->entityHandle.value == k_cueInvalidHandleValue)
                {
                    return CueResult_InvalidArgument;
                }
                if (g_engineApi->isEntityValid(a_createInfo->entityHandle) == 0)
                {
                    return CueResult_NotFound;
                }
                if (!string_view_equals(a_createInfo->scriptName, "RotateCube"))
                {
                    return CueResult_NotFound;
                }

                const uint64_t instanceId = g_nextInstanceId++;
                g_instances.emplace(instanceId, ScriptInstance{
                    a_createInfo->entityHandle,
                    0.0f
                });
                a_outInstanceHandle->value = instanceId;
                return CueResult_Ok;
            };
        a_outExports->destroyScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle) -> CueResult
            {
                if (a_instanceHandle.value == k_cueInvalidHandleValue)
                {
                    return CueResult_InvalidArgument;
                }

                const size_t erased = g_instances.erase(a_instanceHandle.value);
                return erased > 0 ? CueResult_Ok : CueResult_NotFound;
            };
        a_outExports->updateScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                float a_deltaTimeSeconds) -> CueResult
            {
                if (g_engineApi == nullptr)
                {
                    return CueResult_InvalidState;
                }
                if (a_instanceHandle.value == k_cueInvalidHandleValue)
                {
                    return CueResult_InvalidArgument;
                }

                const auto instanceIt = g_instances.find(a_instanceHandle.value);
                if (instanceIt == g_instances.end())
                {
                    return CueResult_NotFound;
                }

                ScriptInstance& instance = instanceIt->second;
                if (g_engineApi->isEntityValid(instance.entityHandle) == 0)
                {
                    return CueResult_NotFound;
                }
                if (g_engineApi->hasTransform(instance.entityHandle) == 0)
                {
                    return CueResult_NotFound;
                }

                CueTransformData transform{};
                CueResult result = g_engineApi->getTransform(
                    instance.entityHandle, &transform);
                if (result != CueResult_Ok)
                {
                    return result;
                }

                instance.elapsedSeconds += a_deltaTimeSeconds;
                transform.rotation.y +=
                    a_deltaTimeSeconds * k_rotationSpeedRadiansPerSecond;

                result = g_engineApi->setTransform(
                    instance.entityHandle, &transform);
                if (result != CueResult_Ok)
                {
                    return result;
                }

                return CueResult_Ok;
            };

        return CueResult_Ok;
    }
}
