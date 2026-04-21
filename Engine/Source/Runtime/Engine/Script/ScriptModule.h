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
        [[nodiscard]] Result load_shadow_copy(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_shadowModulePath) noexcept;
        void unload() noexcept;

        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] const CueScriptExports* exports() const noexcept;
        [[nodiscard]] const Core::IO::Path& module_path() const noexcept;
        [[nodiscard]] MarionnetteObject* get_script_instance_object(
            CueScriptInstanceHandle a_instanceHandle) const noexcept;

        [[nodiscard]] Result register_scripts(
            const CueEngineApi& a_engineApi) const noexcept;

    private:
        [[nodiscard]] Result load_internal(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_loadPath) noexcept;

        void* m_nativeHandle = nullptr;
        Core::IO::Path m_modulePath{};
        Core::IO::Path m_loadedModulePath{};
        CueScriptExports m_exports{};
    };
}
