#include <Native/ScriptModuleRuntime.h>
#include <Script/ScriptClassRegistry.h>
#include <Script/ScriptModuleApi.h>

// === C++ includes ===
#include <span>
#include <string_view>

namespace
{
    Cue::Core::Native::ScriptModuleRuntime g_runtime{};

    [[nodiscard]] Cue::Script::ScriptResult convert_result(
        Cue::Core::Native::ScriptAbiResult a_result) noexcept
    {
        switch (a_result)
        {
        case Cue::Core::Native::ScriptAbiResult::Ok:
            return Cue::Script::ScriptResult::Ok;
        case Cue::Core::Native::ScriptAbiResult::InvalidArgument:
            return Cue::Script::ScriptResult::InvalidArgument;
        case Cue::Core::Native::ScriptAbiResult::NotFound:
            return Cue::Script::ScriptResult::NotFound;
        case Cue::Core::Native::ScriptAbiResult::InvalidState:
            return Cue::Script::ScriptResult::InvalidState;
        case Cue::Core::Native::ScriptAbiResult::InternalError:
            return Cue::Script::ScriptResult::InternalError;
        }

        return Cue::Script::ScriptResult::InternalError;
    }

    [[nodiscard]] uint8_t CUE_SCRIPT_CALL has_script_class(const char* a_className)
    {
        const Cue::Script::ScriptClassRegistry& registry = Cue::Script::ScriptClassRegistry::instance();
        return registry.is_valid() && a_className != nullptr && registry.has_class(a_className)
                   ? 1u
                   : 0u;
    }

    [[nodiscard]] uint32_t CUE_SCRIPT_CALL get_script_class_count()
    {
        return Cue::Script::ScriptClassRegistry::instance().class_count();
    }

    [[nodiscard]] const char* CUE_SCRIPT_CALL get_script_class_name(uint32_t a_index)
    {
        return Cue::Script::ScriptClassRegistry::instance().class_name(a_index);
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL register_engine_api(
        const Cue::Core::Native::ScriptEngineApi* a_engineApi)
    {
        const Cue::Script::ScriptClassRegistry& registry = Cue::Script::ScriptClassRegistry::instance();
        if (!registry.is_valid())
        {
            return Cue::Script::ScriptResult::InvalidState;
        }

        const std::span<const Cue::Core::Native::ScriptClassDefinition> definitions =
            registry.class_definitions();
        return convert_result(g_runtime.register_scripts(
            a_engineApi, definitions.data(), static_cast<uint32_t>(definitions.size())));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL create_script_instance(
        const Cue::Script::ScriptInstanceCreateInfo* a_createInfo,
        Cue::Script::ScriptInstanceHandle* a_outHandle)
    {
        if (a_createInfo == nullptr || a_outHandle == nullptr || a_createInfo->className == nullptr)
        {
            return Cue::Script::ScriptResult::InvalidArgument;
        }

        *a_outHandle = Cue::Script::k_invalidScriptInstanceHandle;
        const std::string_view className(a_createInfo->className);
        Cue::Core::Native::ScriptCreateInfo createInfo{};
        createInfo.entity = {a_createInfo->entityId, a_createInfo->generation};
        createInfo.className = {className.data(), static_cast<uint32_t>(className.size())};

        Cue::Core::Native::ScriptInstanceHandle instanceHandle{};
        const Cue::Script::ScriptResult result = convert_result(
            g_runtime.create_instance(&createInfo, &instanceHandle));
        if (result == Cue::Script::ScriptResult::Ok)
        {
            *a_outHandle = instanceHandle.value;
        }
        return result;
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL destroy_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle)
    {
        return convert_result(g_runtime.destroy_instance({a_handle}));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL start_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle)
    {
        return convert_result(g_runtime.start_instance({a_handle}));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL update_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle,
        float a_deltaTimeSeconds)
    {
        return convert_result(g_runtime.update_instance({a_handle}, a_deltaTimeSeconds));
    }
} // namespace

extern "C"
{
    // Engine と DLL の ABI 世代を先に照合し、異なる exports 構造体を呼び出さない
    __declspec(dllexport) uint32_t CUE_SCRIPT_CALL cue_script_get_abi_version()
    {
        return Cue::Script::k_scriptModuleAbiVersion;
    }

    __declspec(dllexport) Cue::Script::ScriptResult CUE_SCRIPT_CALL cue_script_get_exports(
        Cue::Script::ScriptModuleExports* a_outExports)
    {
        if (a_outExports == nullptr)
        {
            return Cue::Script::ScriptResult::InvalidArgument;
        }
        if (!Cue::Script::ScriptClassRegistry::instance().is_valid())
        {
            return Cue::Script::ScriptResult::InvalidState;
        }

        // class 定義と Engine API を同じ DLL 内の runtime へ集約し、Script state の所有権を越境させない
        a_outExports->structSize = sizeof(Cue::Script::ScriptModuleExports);
        a_outExports->abiVersion = Cue::Script::k_scriptModuleAbiVersion;
        a_outExports->hasClass = &has_script_class;
        a_outExports->getClassCount = &get_script_class_count;
        a_outExports->getClassName = &get_script_class_name;
        a_outExports->registerEngineApi = &register_engine_api;
        a_outExports->createInstance = &create_script_instance;
        a_outExports->destroyInstance = &destroy_script_instance;
        a_outExports->onCreate = &start_script_instance;
        a_outExports->onUpdate = &update_script_instance;
        return Cue::Script::ScriptResult::Ok;
    }
}
