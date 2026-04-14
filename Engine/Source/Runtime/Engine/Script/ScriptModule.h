#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/Path.h>
#include <Native/ScriptAbi.h>

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
        void unload() noexcept;

        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] const CueScriptExports* exports() const noexcept;
        [[nodiscard]] const Core::IO::Path& module_path() const noexcept;

        [[nodiscard]] Result register_scripts(
            const CueEngineApi& a_engineApi) const noexcept;

    private:
        void* m_nativeHandle = nullptr;
        Core::IO::Path m_modulePath{};
        CueScriptExports m_exports{};
    };
}
