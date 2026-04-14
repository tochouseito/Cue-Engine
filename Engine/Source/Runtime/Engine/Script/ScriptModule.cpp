#include "ScriptModule.h"

// === Windows includes ===
#include <windows.h>

// === C++ includes ===
#include <string>

namespace Cue
{
    namespace
    {
        [[nodiscard]] Result utf8_to_wide(
            std::string_view a_text,
            std::wstring& a_outText) noexcept
        {
            if (a_text.empty())
            {
                a_outText.clear();
                return Result::ok();
            }

            const int needed = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()),
                nullptr, 0);
            if (needed <= 0)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Script module path UTF-8 to wide conversion size query failed.");
            }

            a_outText.assign(static_cast<size_t>(needed), L'\0');
            const int written = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()),
                a_outText.data(), needed);
            if (written != needed)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Script module path UTF-8 to wide conversion failed.");
            }

            return Result::ok();
        }

        [[nodiscard]] Result convert_script_result(CueResult a_result) noexcept
        {
            switch (a_result)
            {
            case CueResult_Ok:
                return Result::ok();

            case CueResult_InvalidArgument:
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Script module returned InvalidArgument.");

            case CueResult_NotFound:
                return Result::fail(Code::NotFound, Severity::Error,
                    "Script module returned NotFound.");

            case CueResult_Unsupported:
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Script module returned Unsupported.");

            case CueResult_InvalidState:
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Script module returned InvalidState.");

            case CueResult_InternalError:
                return Result::fail(Code::InternalError, Severity::Error,
                    "Script module returned InternalError.");
            }

            return Result::fail(Code::UnknownError, Severity::Error,
                "Script module returned an unknown result code.");
        }
    }

    ScriptModule::~ScriptModule()
    {
        unload();
    }

    Result ScriptModule::load(const Core::IO::Path& a_modulePath) noexcept
    {
        unload();

        std::wstring wideModulePath{};
        Result result = utf8_to_wide(a_modulePath.utf8(), wideModulePath);
        if (!result)
        {
            return result;
        }

        HMODULE moduleHandle = ::LoadLibraryW(wideModulePath.c_str());
        if (moduleHandle == nullptr)
        {
            return Result::fail(Code::InitializeFailed, Severity::Error,
                "Script module could not be loaded.");
        }

        const auto getAbiVersion =
            reinterpret_cast<CueScriptAbiVersion(CUE_SCRIPT_CALL*)(void)>(
                ::GetProcAddress(moduleHandle, "cue_script_get_abi_version"));
        const auto getExports =
            reinterpret_cast<CueResult(CUE_SCRIPT_CALL*)(CueScriptExports*)>(
                ::GetProcAddress(moduleHandle, "cue_script_get_exports"));

        if (getAbiVersion == nullptr || getExports == nullptr)
        {
            ::FreeLibrary(moduleHandle);
            return Result::fail(Code::NotFound, Severity::Error,
                "Script module exports are missing.");
        }

        const CueScriptAbiVersion abiVersion = getAbiVersion();
        if (abiVersion != k_cueScriptAbiVersion)
        {
            ::FreeLibrary(moduleHandle);
            return Result::fail(Code::Unsupported, Severity::Error,
                "Script module ABI version is not supported.");
        }

        CueScriptExports exports{};
        const CueResult getExportsResult = getExports(&exports);
        result = convert_script_result(getExportsResult);
        if (!result)
        {
            ::FreeLibrary(moduleHandle);
            return result;
        }

        if (exports.structSize < sizeof(CueScriptExports) ||
            exports.abiVersion != k_cueScriptAbiVersion ||
            exports.createScriptInstance == nullptr ||
            exports.destroyScriptInstance == nullptr ||
            exports.updateScriptInstance == nullptr)
        {
            ::FreeLibrary(moduleHandle);
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script module exports are invalid.");
        }

        m_nativeHandle = moduleHandle;
        m_modulePath = a_modulePath;
        m_exports = exports;
        return Result::ok();
    }

    void ScriptModule::unload() noexcept
    {
        if (m_nativeHandle != nullptr)
        {
            ::FreeLibrary(static_cast<HMODULE>(m_nativeHandle));
            m_nativeHandle = nullptr;
        }

        m_modulePath = {};
        m_exports = {};
    }

    bool ScriptModule::is_loaded() const noexcept
    {
        return m_nativeHandle != nullptr;
    }

    const CueScriptExports* ScriptModule::exports() const noexcept
    {
        return is_loaded() ? &m_exports : nullptr;
    }

    const Core::IO::Path& ScriptModule::module_path() const noexcept
    {
        return m_modulePath;
    }

    Result ScriptModule::register_scripts(
        const CueEngineApi& a_engineApi) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script module is not loaded.");
        }
        if (m_exports.registerScripts == nullptr)
        {
            return Result::ok();
        }

        return convert_script_result(m_exports.registerScripts(&a_engineApi));
    }
}
