// ScriptModule の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/Path.h>
#include <Native/ScriptAbi.h>

namespace Cue
{
    class MarionnetteObject;
}

namespace Cue
{
    class ScriptModule final
    {
    public:
        ScriptModule() = default;
        ~ScriptModule();

        ScriptModule(const ScriptModule&) = delete;
        ScriptModule& operator=(const ScriptModule&) = delete;
        ScriptModule(ScriptModule&&) = delete;
        ScriptModule& operator=(ScriptModule&&) = delete;

        [[nodiscard]] Result load(const Core::IO::Path& a_modulePath) noexcept;
        [[nodiscard]] Result load_static(
            CueScriptAbiVersion(CUE_SCRIPT_CALL* a_getAbiVersion)(void),
            CueResult(CUE_SCRIPT_CALL* a_getExports)(CueScriptExports*)) noexcept;
        [[nodiscard]] Result load_shadow_copy(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_shadowModulePath) noexcept;
        void unload() noexcept;

        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] static bool is_loaded(
            const ScriptModule* a_module) noexcept;
        [[nodiscard]] const CueScriptExports* exports() const noexcept;
        [[nodiscard]] const Core::IO::Path& module_path() const noexcept;
        [[nodiscard]] MarionnetteObject* get_script_instance_object(
            CueScriptInstanceHandle a_instanceHandle) const noexcept;

        [[nodiscard]] Result register_scripts(
            const CueEngineApi& a_engineApi) const noexcept;

    private:
        [[nodiscard]] Result initialize_exports(
            CueScriptAbiVersion a_abiVersion,
            CueResult a_getExportsResult,
            const CueScriptExports& a_exports,
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_loadPath) noexcept;
        [[nodiscard]] Result load_internal(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_loadPath) noexcept;

        void* m_nativeHandle = nullptr;
        bool m_isStaticModule = false;
        Core::IO::Path m_modulePath{};
        Core::IO::Path m_loadedModulePath{};
        CueScriptExports m_exports{};
    };
}
