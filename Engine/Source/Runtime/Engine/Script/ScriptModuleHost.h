#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>
#include <Native/ScriptAbi.h>

// === Engine includes ===
#include "ScriptShadowCopyService.h"
#include "../GameCore/Components.h"

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue
{
    struct MarionnetteClass;

    class ScriptModule;
    class ScriptRuntime;
    
    enum class ScriptModuleBuildConfiguration : uint8_t
    {
        Debug,
        RelWithDebInfo,
        Release
    };

    class ScriptModuleHost final
    {
    public:
        struct ScriptReloadReport final
        {
            uint32_t restoredStateCount = 0;
            uint32_t skippedStateCount = 0;
            std::vector<std::string> warnings{};
        };

        explicit ScriptModuleHost(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~ScriptModuleHost();

        ScriptModuleHost(const ScriptModuleHost&) = delete;
        ScriptModuleHost& operator=(const ScriptModuleHost&) = delete;
        ScriptModuleHost(ScriptModuleHost&&) = delete;
        ScriptModuleHost& operator=(ScriptModuleHost&&) = delete;

        [[nodiscard]] Result initialize(GameCore::GameWorld& a_gameWorld) noexcept;
        [[nodiscard]] Result set_game_world(GameCore::GameWorld& a_gameWorld) noexcept;
        void activate_runtime() noexcept;

        [[nodiscard]] ScriptRuntime* runtime() noexcept;
        [[nodiscard]] const ScriptRuntime* runtime() const noexcept;
        [[nodiscard]] const std::vector<std::string>&
            registered_script_classes() const noexcept;
        [[nodiscard]] bool has_registered_script_class(
            std::string_view a_className) const noexcept;
        [[nodiscard]] const std::vector<ECS::ScriptFieldValue>&
            script_field_defaults(std::string_view a_className) const noexcept;
        [[nodiscard]] const MarionnetteClass* find_marionnette_class(
            std::string_view a_className) const noexcept;
        [[nodiscard]] const ScriptReloadReport&
            last_reload_report() const noexcept;

        [[nodiscard]] Result load_module(
            const Core::IO::Path& a_scriptRoot,
            ScriptModuleBuildConfiguration a_configuration,
            GameCore::GameWorld& a_validationWorld) noexcept;
        [[nodiscard]] Result load_static_module(
            CueScriptAbiVersion(CUE_SCRIPT_CALL* a_getAbiVersion)(void),
            CueResult(CUE_SCRIPT_CALL* a_getExports)(CueScriptExports*),
            GameCore::GameWorld& a_validationWorld) noexcept;
        void unload_module() noexcept;

    private:
        [[nodiscard]] Result activate_loaded_module(
            std::unique_ptr<ScriptModule> a_nextModule,
            const Core::IO::Path& a_scriptRoot,
            GameCore::GameWorld& a_validationWorld) noexcept;
        [[nodiscard]] Result resolve_script_module_path(
            const Core::IO::Path& a_scriptRoot,
            ScriptModuleBuildConfiguration a_configuration,
            Core::IO::Path& a_outModulePath) noexcept;

    private:
        Core::IO::IFileSystem& m_fileSystem;
        ScriptShadowCopyService m_shadowCopyService;
        std::unique_ptr<ScriptModule> m_module = nullptr;
        std::unique_ptr<ScriptRuntime> m_runtime = nullptr;
        ScriptReloadReport m_lastReloadReport{};
        Core::IO::Path m_scriptRoot{};
        uint64_t m_shadowCopyId = 0;
    };
}
