#include "ScriptModuleHost.h"

// === Engine includes ===
#include "../GameCore/GameWorld.h"
#include "ScriptModule.h"
#include "ScriptRuntime.h"

// === C++ includes ===
#include <array>
#include <memory>

namespace Cue
{
    namespace
    {
        [[nodiscard]] const char* to_build_configuration_name(
            ScriptModuleBuildConfiguration a_configuration) noexcept
        {
            switch (a_configuration)
            {
            case ScriptModuleBuildConfiguration::Debug:
                return "Debug";

            case ScriptModuleBuildConfiguration::RelWithDebInfo:
                return "RelWithDebInfo";

            case ScriptModuleBuildConfiguration::Release:
                return "Release";
            }

            return "Debug";
        }
    }

    ScriptModuleHost::ScriptModuleHost(
        Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(a_fileSystem)
        , m_shadowCopyService(a_fileSystem)
    {
    }

    ScriptModuleHost::~ScriptModuleHost()
    {
        unload_module();
    }

    Result ScriptModuleHost::initialize(GameCore::GameWorld& a_gameWorld) noexcept
    {
        if (m_runtime != nullptr)
        {
            return set_game_world(a_gameWorld);
        }

        m_module = std::make_unique<ScriptModule>();
        m_runtime = std::make_unique<ScriptRuntime>(a_gameWorld);
        return Result::ok();
    }

    Result ScriptModuleHost::set_game_world(GameCore::GameWorld& a_gameWorld) noexcept
    {
        if (m_runtime == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime is not initialized.");
        }

        return m_runtime->set_game_world(a_gameWorld);
    }

    void ScriptModuleHost::activate_runtime() noexcept
    {
        if (m_runtime != nullptr)
        {
            m_runtime->activate();
        }
    }

    ScriptRuntime* ScriptModuleHost::runtime() noexcept
    {
        return m_runtime.get();
    }

    const ScriptRuntime* ScriptModuleHost::runtime() const noexcept
    {
        return m_runtime.get();
    }

    const std::vector<std::string>&
        ScriptModuleHost::registered_script_classes() const noexcept
    {
        static const std::vector<std::string> k_empty{};
        return m_runtime != nullptr
            ? m_runtime->registered_script_classes()
            : k_empty;
    }

    bool ScriptModuleHost::has_registered_script_class(
        std::string_view a_className) const noexcept
    {
        return m_runtime != nullptr &&
            m_runtime->has_registered_script_class(a_className);
    }

    const std::vector<ECS::ScriptFieldValue>&
        ScriptModuleHost::script_field_defaults(
            std::string_view a_className) const noexcept
    {
        static const std::vector<ECS::ScriptFieldValue> k_empty{};
        return m_runtime != nullptr
            ? m_runtime->script_field_defaults(a_className)
            : k_empty;
    }

    const MarionnetteClass* ScriptModuleHost::find_marionnette_class(
        std::string_view a_className) const noexcept
    {
        return m_runtime != nullptr
            ? m_runtime->find_marionnette_class(a_className)
            : nullptr;
    }

    const ScriptModuleHost::ScriptReloadReport&
        ScriptModuleHost::last_reload_report() const noexcept
    {
        return m_lastReloadReport;
    }

    Result ScriptModuleHost::load_module(
        const Core::IO::Path& a_scriptRoot,
        ScriptModuleBuildConfiguration a_configuration,
        GameCore::GameWorld& a_validationWorld) noexcept
    {
        m_lastReloadReport = {};

        if (m_module == nullptr || m_runtime == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script module host is not initialized.");
        }

        Core::IO::Path modulePath{};
        Result result = resolve_script_module_path(
            a_scriptRoot, a_configuration, modulePath);
        if (!result)
        {
            return result;
        }

        Core::IO::Path shadowModulePath{};
        result = m_shadowCopyService.create_shadow_copy(
            a_scriptRoot,
            modulePath,
            ++m_shadowCopyId,
            shadowModulePath);
        if (!result)
        {
            return result;
        }

        std::unique_ptr<ScriptModule> nextModule = std::make_unique<ScriptModule>();
        result = nextModule->load_shadow_copy(modulePath, shadowModulePath);
        if (!result)
        {
            return result;
        }

        return activate_loaded_module(
            std::move(nextModule), a_scriptRoot, a_validationWorld);
    }

    Result ScriptModuleHost::load_static_module(
        CueScriptAbiVersion(CUE_SCRIPT_CALL* a_getAbiVersion)(void),
        CueResult(CUE_SCRIPT_CALL* a_getExports)(CueScriptExports*),
        GameCore::GameWorld& a_validationWorld) noexcept
    {
        m_lastReloadReport = {};

        if (m_module == nullptr || m_runtime == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script module host is not initialized.");
        }

        std::unique_ptr<ScriptModule> nextModule = std::make_unique<ScriptModule>();
        Result result = nextModule->load_static(a_getAbiVersion, a_getExports);
        if (!result)
        {
            return result;
        }

        return activate_loaded_module(
            std::move(nextModule), Core::IO::Path("[static]"), a_validationWorld);
    }

    Result ScriptModuleHost::activate_loaded_module(
        std::unique_ptr<ScriptModule> a_nextModule,
        const Core::IO::Path& a_scriptRoot,
        GameCore::GameWorld& a_validationWorld) noexcept
    {
        Result result{};

        {
            ScriptRuntime validationRuntime(a_validationWorld);
            result = a_nextModule->register_scripts(validationRuntime.engine_api());
        }
        activate_runtime();
        if (!result)
        {
            a_nextModule->unload();
            return result;
        }

        std::vector<ScriptRuntime::StateSnapshot> preservedStateSnapshots{};
        result = m_runtime->capture_instance_states(preservedStateSnapshots);
        if (!result)
        {
            a_nextModule->unload();
            return result;
        }

        result = m_runtime->reset();
        if (!result)
        {
            a_nextModule->unload();
            return result;
        }

        std::unique_ptr<ScriptModule> previousModule = std::move(m_module);
        m_module = std::move(a_nextModule);
        m_runtime->set_module(m_module.get());
        result = m_module->register_scripts(m_runtime->engine_api());
        if (!result)
        {
            m_runtime->set_module(nullptr);
            m_module->unload();
            m_module = std::move(previousModule);
            if (m_module != nullptr)
            {
                m_runtime->set_module(m_module.get());
                const Result restoreResult =
                    m_module->register_scripts(m_runtime->engine_api());
                if (!restoreResult)
                {
                    m_runtime->set_module(nullptr);
                    m_module->unload();
                    m_module = nullptr;
                    m_scriptRoot = {};
                }
            }
            return result;
        }

        ScriptRuntime::StateRestoreReport stateRestoreReport{};
        result = m_runtime->restore_instance_states(
            preservedStateSnapshots,
            &stateRestoreReport);
        if (!result)
        {
            m_runtime->set_module(nullptr);
            m_module->unload();
            m_module = std::move(previousModule);
            if (m_module != nullptr)
            {
                m_runtime->set_module(m_module.get());
                const Result restoreResult =
                    m_module->register_scripts(m_runtime->engine_api());
                if (!restoreResult)
                {
                    m_runtime->set_module(nullptr);
                    m_module->unload();
                    m_module = nullptr;
                    m_scriptRoot = {};
                }
            }
            return result;
        }

        m_lastReloadReport.restoredStateCount = stateRestoreReport.restoredCount;
        m_lastReloadReport.skippedStateCount = stateRestoreReport.skippedCount;
        m_lastReloadReport.warnings.reserve(stateRestoreReport.issues.size());
        for (const ScriptRuntime::StateRestoreIssue& issue :
            stateRestoreReport.issues)
        {
            m_lastReloadReport.warnings.push_back(
                std::string("state restore skipped: entity=") +
                std::to_string(issue.entityId) +
                ", class=" + issue.className +
                ", reason=" + issue.detail);
        }

        if (previousModule != nullptr)
        {
            previousModule->unload();
        }

        m_scriptRoot = a_scriptRoot;
        return Result::ok();
    }

    void ScriptModuleHost::unload_module() noexcept
    {
        if (m_runtime != nullptr)
        {
            (void)m_runtime->reset();
            m_runtime->set_module(nullptr);
        }

        if (m_module != nullptr)
        {
            m_module->unload();
        }

        m_scriptRoot = {};
    }

    Result ScriptModuleHost::resolve_script_module_path(
        const Core::IO::Path& a_scriptRoot,
        ScriptModuleBuildConfiguration a_configuration,
        Core::IO::Path& a_outModulePath) noexcept
    {
        a_outModulePath = {};
        const char* buildConfigurationName =
            to_build_configuration_name(a_configuration);

        const std::array<Core::IO::Path, 4> candidatePaths = {
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(
                    std::string("Binaries/") + buildConfigurationName +
                    "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("out/build/win-x64/GameScript/") +
                    buildConfigurationName + "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("out/build/win-x64/") +
                    buildConfigurationName + "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("generated/outputs/") +
                    buildConfigurationName + "/GameScript.dll")),
        };

        bool hasResolvedCandidate = false;
        int64_t latestModifiedTime = 0;

        for (const Core::IO::Path& candidatePath : candidatePaths)
        {
            bool exists = false;
            Result result = m_fileSystem.exists(candidatePath, &exists);
            if (!result)
            {
                return result;
            }

            if (exists)
            {
                Core::IO::FileStat fileStat{};
                result = m_fileSystem.stat(candidatePath, &fileStat);
                if (!result)
                {
                    return result;
                }

                if (!hasResolvedCandidate ||
                    fileStat.mtime_ns > latestModifiedTime)
                {
                    a_outModulePath = candidatePath;
                    latestModifiedTime = fileStat.mtime_ns;
                    hasResolvedCandidate = true;
                }
            }
        }

        if (hasResolvedCandidate)
        {
            return Result::ok();
        }

        return Result::fail(Code::NotFound, Severity::Warning,
            "GameScript.dll was not found in the script root.");
    }
}
