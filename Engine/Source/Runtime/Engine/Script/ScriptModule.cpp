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
                   a_exports.registerEngineApi != nullptr &&
                   a_exports.getClassFieldCount != nullptr &&
                   a_exports.getClassField != nullptr &&
                   a_exports.getClassFunctionCount != nullptr &&
                   a_exports.getClassFunctionName != nullptr &&
                   a_exports.invokeInstance != nullptr &&
                   a_exports.getStateDescriptor != nullptr &&
                   a_exports.getInstanceStateSize != nullptr &&
                   a_exports.serializeInstance != nullptr &&
                   a_exports.restoreInstance != nullptr;
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

        [[nodiscard]] Result collect_class_infos(
            const ScriptModuleExports& a_exports,
            const std::vector<std::string>& a_classNames,
            std::vector<ScriptClassInfo>& a_outClassInfos)
        {
            constexpr uint32_t k_maxFieldsPerClass = 4096u;
            constexpr uint32_t k_maxFunctionsPerClass = 4096u;

            a_outClassInfos.clear();
            a_outClassInfos.reserve(a_classNames.size());
            for (const std::string& className : a_classNames)
            {
                ScriptClassInfo classInfo{};
                classInfo.name = className;

                const uint32_t fieldCount =
                    a_exports.getClassFieldCount(className.c_str());
                const uint32_t functionCount =
                    a_exports.getClassFunctionCount(className.c_str());
                if (fieldCount > k_maxFieldsPerClass ||
                    functionCount > k_maxFunctionsPerClass)
                {
                    return Result::fail(
                        Code::InvalidState, Severity::Error,
                        "Script class registered too many fields or functions.");
                }

                classInfo.fields.reserve(fieldCount);
                for (uint32_t index = 0u; index < fieldCount; ++index)
                {
                    Core::Native::ScriptFieldValue field{};
                    const ScriptResult fieldResult =
                        a_exports.getClassField(
                            className.c_str(), index, &field);
                    if (fieldResult != ScriptResult::Ok ||
                        field.name.data == nullptr ||
                        field.name.size == 0u)
                    {
                        return Result::fail(
                            Code::InvalidState, Severity::Error,
                            "Script field metadata is invalid.");
                    }

                    ScriptFieldInfo fieldInfo{};
                    fieldInfo.name.assign(field.name.data, field.name.size);
                    fieldInfo.type = field.type;
                    fieldInfo.flags = field.flags;
                    fieldInfo.floatValue = field.floatValue;
                    fieldInfo.int32Value = field.int32Value;
                    fieldInfo.boolValue = field.boolValue != 0u;
                    fieldInfo.entityValue = field.entityValue;
                    if (field.classValue.data != nullptr &&
                        field.classValue.size > 0u)
                    {
                        fieldInfo.classValue.assign(
                            field.classValue.data, field.classValue.size);
                    }
                    const auto duplicate = std::find_if(
                        classInfo.fields.begin(), classInfo.fields.end(),
                        [&fieldInfo](const ScriptFieldInfo& a_value)
                        {
                            return a_value.name == fieldInfo.name;
                        });
                    if (duplicate != classInfo.fields.end())
                    {
                        return Result::fail(
                            Code::InvalidState, Severity::Error,
                            "Script class registered a duplicate field.");
                    }
                    classInfo.fields.push_back(std::move(fieldInfo));
                }

                classInfo.functions.reserve(functionCount);
                for (uint32_t index = 0u; index < functionCount; ++index)
                {
                    const char* functionName =
                        a_exports.getClassFunctionName(
                            className.c_str(), index);
                    if (functionName == nullptr || functionName[0] == '\0')
                    {
                        return Result::fail(
                            Code::InvalidState, Severity::Error,
                            "Script function metadata is invalid.");
                    }
                    if (std::find(
                            classInfo.functions.begin(),
                            classInfo.functions.end(),
                            functionName) != classInfo.functions.end())
                    {
                        return Result::fail(
                            Code::InvalidState, Severity::Error,
                            "Script class registered a duplicate function.");
                    }
                    classInfo.functions.emplace_back(functionName);
                }

                if (a_exports.getStateDescriptor(
                        className.c_str(), &classInfo.stateDescriptor) !=
                    ScriptResult::Ok)
                {
                    return Result::fail(
                        Code::InvalidState, Severity::Error,
                        "Script state descriptor is invalid.");
                }

                a_outClassInfos.push_back(std::move(classInfo));
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

        std::vector<ScriptClassInfo> classInfos{};
        result = collect_class_infos(exports, classNames, classInfos);
        if (!result)
        {
            ::FreeLibrary(moduleHandle);
            return result;
        }

        // 全ての契約を確認してから公開状態へ遷移し、失敗した DLL の関数を呼べないようにする
        m_nativeHandle = moduleHandle;
        m_exports = exports;
        m_classNames = std::move(classNames);
        m_classInfos = std::move(classInfos);
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
        m_classInfos.clear();
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

    const std::vector<ScriptClassInfo>& ScriptModule::class_infos() const noexcept
    {
        return m_classInfos;
    }

    const ScriptClassInfo* ScriptModule::find_class_info(
        std::string_view a_className) const noexcept
    {
        const auto classInfo = std::find_if(
            m_classInfos.begin(), m_classInfos.end(),
            [a_className](const ScriptClassInfo& a_value)
            {
                return a_value.name == a_className;
            });
        return classInfo != m_classInfos.end() ? &*classInfo : nullptr;
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

    Result ScriptModule::invoke(
        ScriptInstanceHandle a_handle,
        std::string_view a_functionName) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }
        if (a_handle == k_invalidScriptInstanceHandle ||
            a_functionName.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Script function invocation is invalid.");
        }

        const std::string functionName(a_functionName);
        return convert_result(
            m_exports.invokeInstance(a_handle, functionName.c_str()));
    }

    Result ScriptModule::get_state_descriptor(
        std::string_view a_className,
        Core::Native::ScriptStateDescriptor& a_outDescriptor) const noexcept
    {
        if (!is_loaded() || a_className.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module or class name is invalid.");
        }

        const std::string className(a_className);
        return convert_result(
            m_exports.getStateDescriptor(
                className.c_str(), &a_outDescriptor));
    }

    Result ScriptModule::get_instance_state_size(
        ScriptInstanceHandle a_handle,
        uint32_t& a_outStateSize) const noexcept
    {
        a_outStateSize = 0u;
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(
            m_exports.getInstanceStateSize(a_handle, &a_outStateSize));
    }

    Result ScriptModule::serialize_instance(
        ScriptInstanceHandle a_handle,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(m_exports.serializeInstance(
            a_handle, a_outStateBuffer, a_stateBufferSize));
    }

    Result ScriptModule::restore_instance(
        ScriptInstanceHandle a_handle,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize) const noexcept
    {
        if (!is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        return convert_result(m_exports.restoreInstance(
            a_handle, a_stateBuffer, a_stateBufferSize));
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
