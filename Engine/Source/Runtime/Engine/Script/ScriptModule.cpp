#include "ScriptModule.h"

// === Windows includes ===
#if defined(_WIN32)
#include <windows.h>
#endif

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Cue::Script
{
    namespace
    {
        // Win32 API は UTF-16 のパスを受け取るため、Path の UTF-8 表現を DLL ロード直前に変換する
        [[nodiscard]] Result utf8_to_wide(std::string_view a_text, std::wstring& a_outText) noexcept
        {
#if defined(_WIN32)
            const int requiredLength = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                                    "Script module path UTF-8 conversion failed.");
            }

            a_outText.assign(static_cast<size_t>(requiredLength), L'\0');
            const int writtenLength = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()),
                a_outText.data(), requiredLength);
            if (writtenLength != requiredLength)
            {
                return Result::fail(Code::InternalError, Severity::Error,
                                    "Script module path UTF-8 conversion was incomplete.");
            }

            return Result::ok();
#else
            (void)a_text;
            (void)a_outText;
            return Result::fail(Code::Unsupported, Severity::Error,
                                "Dynamic script modules are not supported on this platform.");
#endif
        }

        void delete_file_if_exists(const Core::IO::Path& a_path) noexcept
        {
#if defined(_WIN32)
            if (a_path.is_empty())
            {
                return;
            }

            std::wstring path{};
            const Result result = utf8_to_wide(a_path.utf8(), path);
            if (result)
            {
                (void)::DeleteFileW(path.c_str());
            }
#else
            (void)a_path;
#endif
        }

        // ScriptRuntime が呼び出す全関数を先に確認し、部分的な exports を実行中に露出させない
        constexpr uint32_t k_maxScriptClassCount = 4096u;

        [[nodiscard]] bool has_required_exports(const ScriptModuleExports& a_exports) noexcept
        {
            return a_exports.structSize >= sizeof(ScriptModuleExports) &&
                   a_exports.abiVersion == k_scriptModuleAbiVersion &&
                   a_exports.hasClass != nullptr &&
                   a_exports.createInstance != nullptr &&
                   a_exports.destroyInstance != nullptr &&
                   a_exports.onCreate != nullptr &&
                   a_exports.onUpdate != nullptr &&
                   a_exports.getClassCount != nullptr &&
                   a_exports.getClassName != nullptr &&
                   a_exports.registerEngineApi != nullptr;
        }

        [[nodiscard]] Result collect_class_names(
            const ScriptModuleExports& a_exports,
            std::vector<std::string>& a_outClassNames)
        {
            const uint32_t classCount = a_exports.getClassCount();
            if (classCount > k_maxScriptClassCount)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Script module registered too many classes.");
            }

            a_outClassNames.clear();
            a_outClassNames.reserve(classCount);
            for (uint32_t index = 0u; index < classCount; ++index)
            {
                const char* className = a_exports.getClassName(index);
                if (className == nullptr || className[0] == '\0' ||
                    a_exports.hasClass(className) == 0u)
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                                        "Script module class registration is invalid.");
                }

                const auto existing = std::find(a_outClassNames.begin(), a_outClassNames.end(), className);
                if (existing != a_outClassNames.end())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                                        "Script module registered a duplicate class.");
                }

                a_outClassNames.emplace_back(className);
            }

            return Result::ok();
        }
    } // namespace

    ScriptModule::~ScriptModule()
    {
        unload();
    }

    Result ScriptModule::load(const Core::IO::Path& a_modulePath) noexcept
    {
        return load_internal(a_modulePath, a_modulePath);
    }

    Result ScriptModule::load_shadow_copy(
        const Core::IO::Path& a_modulePath,
        const Core::IO::Path& a_shadowPath) noexcept
    {
        return load_internal(a_modulePath, a_shadowPath);
    }

    Result ScriptModule::load_internal(
        const Core::IO::Path& a_modulePath,
        const Core::IO::Path& a_loadPath) noexcept
    {
        // 同時に複数の GameScript DLL を保持せず、runtime が参照する exports の出所を一つに保つ
        unload();
        if (a_modulePath.is_empty() || a_loadPath.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Script module path is empty.");
        }

        m_modulePath = a_modulePath.normalize();
        m_loadedPath = a_loadPath.normalize();

#if defined(_WIN32)
        std::wstring modulePath{};
        Result result = utf8_to_wide(a_loadPath.utf8(), modulePath);
        if (!result)
        {
            return result;
        }

        HMODULE moduleHandle = ::LoadLibraryW(modulePath.c_str());
        if (moduleHandle == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                                "Script module could not be loaded.");
        }

        const auto getAbiVersion = reinterpret_cast<ScriptGetAbiVersionFn>(
            ::GetProcAddress(moduleHandle, "cue_script_get_abi_version"));
        const auto getExports = reinterpret_cast<ScriptGetExportsFn>(
            ::GetProcAddress(moduleHandle, "cue_script_get_exports"));
        if (getAbiVersion == nullptr || getExports == nullptr)
        {
            ::FreeLibrary(moduleHandle);
            return Result::fail(Code::NotFound, Severity::Error,
                                "Script module required exports were not found.");
        }

        // ABI が異なる DLL は関数の並びや引数の契約が違うため、exports を取得せずに解放する
        if (getAbiVersion() != k_scriptModuleAbiVersion)
        {
            ::FreeLibrary(moduleHandle);
            return Result::fail(Code::Unsupported, Severity::Error,
                                "Script module ABI version is not supported.");
        }

        ScriptModuleExports exports{};
        result = convert_result(getExports(&exports));
        if (!result || !has_required_exports(exports))
        {
            ::FreeLibrary(moduleHandle);
            return result ? Result::fail(Code::InvalidState, Severity::Error,
                                         "Script module exports are invalid.")
                          : result;
        }

        std::vector<std::string> classNames{};
        result = collect_class_names(exports, classNames);
        if (!result)
        {
            ::FreeLibrary(moduleHandle);
            return result;
        }

        // 全ての契約を確認してから公開状態へ遷移し、失敗した DLL の関数を呼べないようにする
        m_nativeHandle = moduleHandle;
        m_exports = exports;
        m_classNames = std::move(classNames);
        return Result::ok();
#else
        std::wstring unusedPath{};
        return utf8_to_wide(a_modulePath.utf8(), unusedPath);
#endif
    }

    void ScriptModule::unload() noexcept
    {
#if defined(_WIN32)
        if (m_nativeHandle != nullptr)
        {
            ::FreeLibrary(static_cast<HMODULE>(m_nativeHandle));
        }
#endif
        if (!m_loadedPath.is_empty() && m_loadedPath.utf8() != m_modulePath.utf8())
        {
            delete_file_if_exists(m_loadedPath);
            delete_file_if_exists(Core::IO::Path::join(
                m_loadedPath.parent(),
                Core::IO::Path(m_loadedPath.stem() + ".pdb")));
        }
        // DLL 解放後の関数ポインタを誤用しないよう、関連状態を同じ箇所で初期化する
        m_nativeHandle = nullptr;
        m_modulePath = {};
        m_loadedPath = {};
        m_exports = {};
        m_classNames.clear();
    }

    bool ScriptModule::is_loaded() const noexcept
    {
        return m_nativeHandle != nullptr;
    }

    bool ScriptModule::has_class(const char* a_className) const noexcept
    {
        return is_loaded() && a_className != nullptr && a_className[0] != '\0' &&
               m_exports.hasClass(a_className) != 0u;
    }

    const std::vector<std::string>& ScriptModule::class_names() const noexcept
    {
        return m_classNames;
    }

    Result ScriptModule::register_engine_api(
        const Core::Native::ScriptEngineApi& a_engineApi) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(m_exports.registerEngineApi(&a_engineApi));
    }

    Result ScriptModule::create_instance(
        const ScriptInstanceCreateInfo& a_createInfo,
        ScriptInstanceHandle& a_outHandle) const noexcept
    {
        a_outHandle = k_invalidScriptInstanceHandle;
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }
        if (a_createInfo.className == nullptr || a_createInfo.className[0] == '\0')
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Script class name is empty.");
        }

        return convert_result(m_exports.createInstance(&a_createInfo, &a_outHandle));
    }

    Result ScriptModule::destroy_instance(ScriptInstanceHandle a_handle) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }
        if (a_handle == k_invalidScriptInstanceHandle)
        {
            return Result::ok();
        }

        return convert_result(m_exports.destroyInstance(a_handle));
    }

    Result ScriptModule::on_create(ScriptInstanceHandle a_handle) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(m_exports.onCreate(a_handle));
    }

    Result ScriptModule::on_update(
        ScriptInstanceHandle a_handle,
        float a_deltaTimeSeconds) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(m_exports.onUpdate(a_handle, a_deltaTimeSeconds));
    }

    Result ScriptModule::convert_result(ScriptResult a_result) noexcept
    {
        switch (a_result)
        {
        case ScriptResult::Ok:
            return Result::ok();
        case ScriptResult::InvalidArgument:
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Script module rejected an argument.");
        case ScriptResult::NotFound:
            return Result::fail(Code::NotFound, Severity::Error,
                                "Script module could not resolve the requested item.");
        case ScriptResult::InvalidState:
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is in an invalid state.");
        case ScriptResult::InternalError:
            return Result::fail(Code::InternalError, Severity::Error,
                                "Script module reported an internal error.");
        }

        return Result::fail(Code::InternalError, Severity::Error,
                            "Script module returned an unknown result.");
    }
} // namespace Cue::Script
