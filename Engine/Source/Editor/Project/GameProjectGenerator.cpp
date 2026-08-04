#include "GameProjectGenerator.h"

// === Runtime includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === C++ includes ===
#include <cstddef>
#include <string_view>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        constexpr std::string_view k_cmakeListsText = R"(cmake_minimum_required(VERSION 3.24)

project(GameScriptProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project を Engine repository 外へ配置した場合も、cache または環境変数で SDK の場所を固定できるようにする。
set(CUE_ENGINE_ROOT "" CACHE PATH "CueEngine repository root")

if(CUE_ENGINE_ROOT STREQUAL "")
    # Engine と同じ repository 配下へ作成した Project は、親 directory を探索して設定不要で開けるようにする。
    set(_cue_engine_search_root "${CMAKE_CURRENT_SOURCE_DIR}")
    while(TRUE)
        if(EXISTS "${_cue_engine_search_root}/Engine/Source/Runtime/Engine/Script/ScriptModuleApi.h")
            set(CUE_ENGINE_ROOT "${_cue_engine_search_root}")
            break()
        endif()

        get_filename_component(_cue_engine_parent "${_cue_engine_search_root}" DIRECTORY)
        if(_cue_engine_parent STREQUAL _cue_engine_search_root)
            break()
        endif()

        set(_cue_engine_search_root "${_cue_engine_parent}")
    endwhile()
endif()

if(CUE_ENGINE_ROOT STREQUAL "" AND DEFINED ENV{CUE_ENGINE_ROOT})
    set(CUE_ENGINE_ROOT "$ENV{CUE_ENGINE_ROOT}")
endif()

if(NOT EXISTS "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/Script/ScriptModuleApi.h")
    message(FATAL_ERROR
        "CueEngine root not found. Place this project under the CueEngine repository or set CUE_ENGINE_ROOT.")
endif()

file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*.cpp"
)

# Script source の追加を CMake 再構成へ反映し、Editor で生成した Asset を手作業なしで DLL に含める。
add_library(GameScript SHARED
    "EngineModule/GameScriptModule.cpp"
    ${GAME_SCRIPT_SOURCES}
)

target_include_directories(GameScript PRIVATE
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Base"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/ECS"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PlatformAbstractionLayer"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RenderingHardwareInterface"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
)

# GameScript は Engine SDK の Script ABI 実装を利用するため、同じ構成の library をリンクする。
target_link_libraries(GameScript PRIVATE
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Engine.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Base.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Core.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/CueMath.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/ECS.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/PAL.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/RHI.lib"
)

if(MSVC)
    # Engine の Math 型が利用する anonymous union は MSVC 拡張であり、GameScript の警告判定から分離する。
    target_compile_options(GameScript PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

set_target_properties(GameScript PROPERTIES
    OUTPUT_NAME "GameScript"
    # Editor が Project root から build 成果物を一意に解決できるよう、構成別の固定 directory へ出力する。
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/$<CONFIG>"
)
)";

        constexpr std::string_view k_cmakePresetsText = R"({
    "version": 6,
    "configurePresets": [
        {
            "name": "windows-base",
            "hidden": true,
            "generator": "Visual Studio 18 2026",
            "binaryDir": "${sourceDir}/Intermediate/CMake/${presetName}",
            "condition": {
                "type": "equals",
                "lhs": "${hostSystemName}",
                "rhs": "Windows"
            }
        },
        {
            "name": "win-x64",
            "displayName": "Windows x64",
            "inherits": "windows-base",
            "architecture": {
                "value": "x64",
                "strategy": "set"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "win-x64-debug",
            "configurePreset": "win-x64",
            "configuration": "Debug"
        },
        {
            "name": "win-x64-release",
            "configurePreset": "win-x64",
            "configuration": "Release"
        }
    ]
}
)";

        constexpr std::string_view k_moduleText = R"(#include <Native/ScriptModuleRuntime.h>
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

    [[nodiscard]] uint32_t CUE_SCRIPT_CALL get_script_class_field_count(
        const char* a_className)
    {
        if (a_className == nullptr)
        {
            return 0u;
        }

        const Cue::Core::Native::ScriptClassDefinition* definition =
            Cue::Script::ScriptClassRegistry::instance().find_class_definition(
                a_className);
        return definition != nullptr ? definition->fieldCount : 0u;
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL get_script_class_field(
        const char* a_className,
        uint32_t a_index,
        Cue::Core::Native::ScriptFieldValue* a_outField)
    {
        if (a_className == nullptr || a_outField == nullptr)
        {
            return Cue::Script::ScriptResult::InvalidArgument;
        }

        const Cue::Core::Native::ScriptClassDefinition* definition =
            Cue::Script::ScriptClassRegistry::instance().find_class_definition(
                a_className);
        if (definition == nullptr || a_index >= definition->fieldCount)
        {
            return Cue::Script::ScriptResult::NotFound;
        }

        *a_outField = definition->fields[a_index].defaultValue;
        return Cue::Script::ScriptResult::Ok;
    }

    [[nodiscard]] uint32_t CUE_SCRIPT_CALL get_script_class_function_count(
        const char* a_className)
    {
        if (a_className == nullptr)
        {
            return 0u;
        }

        const Cue::Core::Native::ScriptClassDefinition* definition =
            Cue::Script::ScriptClassRegistry::instance().find_class_definition(
                a_className);
        return definition != nullptr ? definition->functionCount : 0u;
    }

    [[nodiscard]] const char* CUE_SCRIPT_CALL get_script_class_function_name(
        const char* a_className,
        uint32_t a_index)
    {
        if (a_className == nullptr)
        {
            return nullptr;
        }

        const Cue::Core::Native::ScriptClassDefinition* definition =
            Cue::Script::ScriptClassRegistry::instance().find_class_definition(
                a_className);
        return definition != nullptr && a_index < definition->functionCount
                   ? definition->functions[a_index].name.data
                   : nullptr;
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
        createInfo.fieldValues = a_createInfo->fieldValues;
        createInfo.fieldCount = a_createInfo->fieldCount;

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

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL invoke_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle,
        const char* a_functionName)
    {
        if (a_functionName == nullptr)
        {
            return Cue::Script::ScriptResult::InvalidArgument;
        }

        const std::string_view functionName(a_functionName);
        return convert_result(g_runtime.invoke_instance(
            {a_handle},
            {functionName.data(), static_cast<uint32_t>(functionName.size())}));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL
    get_script_state_descriptor(
        const char* a_className,
        Cue::Core::Native::ScriptStateDescriptor* a_outDescriptor)
    {
        if (a_className == nullptr || a_outDescriptor == nullptr)
        {
            return Cue::Script::ScriptResult::InvalidArgument;
        }

        const Cue::Core::Native::ScriptClassDefinition* definition =
            Cue::Script::ScriptClassRegistry::instance().find_class_definition(
                a_className);
        if (definition == nullptr)
        {
            return Cue::Script::ScriptResult::NotFound;
        }

        *a_outDescriptor = definition->stateDescriptor;
        return Cue::Script::ScriptResult::Ok;
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL
    get_script_instance_state_size(
        Cue::Script::ScriptInstanceHandle a_handle,
        uint32_t* a_outStateSize)
    {
        return convert_result(
            g_runtime.get_instance_state_size({a_handle}, a_outStateSize));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL
    serialize_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize)
    {
        return convert_result(g_runtime.serialize_instance(
            {a_handle}, a_outStateBuffer, a_stateBufferSize));
    }

    [[nodiscard]] Cue::Script::ScriptResult CUE_SCRIPT_CALL
    restore_script_instance(
        Cue::Script::ScriptInstanceHandle a_handle,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize)
    {
        return convert_result(g_runtime.restore_instance(
            {a_handle}, a_stateBuffer, a_stateBufferSize));
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
        a_outExports->getClassFieldCount = &get_script_class_field_count;
        a_outExports->getClassField = &get_script_class_field;
        a_outExports->getClassFunctionCount = &get_script_class_function_count;
        a_outExports->getClassFunctionName = &get_script_class_function_name;
        a_outExports->registerEngineApi = &register_engine_api;
        a_outExports->createInstance = &create_script_instance;
        a_outExports->destroyInstance = &destroy_script_instance;
        a_outExports->onCreate = &start_script_instance;
        a_outExports->onUpdate = &update_script_instance;
        a_outExports->invokeInstance = &invoke_script_instance;
        a_outExports->getStateDescriptor = &get_script_state_descriptor;
        a_outExports->getInstanceStateSize = &get_script_instance_state_size;
        a_outExports->serializeInstance = &serialize_script_instance;
        a_outExports->restoreInstance = &restore_script_instance;
        return Cue::Script::ScriptResult::Ok;
    }
}
)";

        [[nodiscard]] std::vector<std::byte> to_file_data(std::string_view a_text)
        {
            std::vector<std::byte> data(a_text.size());
            for (size_t index = 0u; index < a_text.size(); ++index)
            {
                data[index] = static_cast<std::byte>(a_text[index]);
            }
            return data;
        }
    } // namespace

    GameProjectGenerator::GameProjectGenerator(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(&a_fileSystem)
    {
    }

    Result GameProjectGenerator::generate(const Core::IO::Path& a_projectRoot) const
    {
        if (m_fileSystem == nullptr || a_projectRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Game project root is invalid.");
        }

        // module entry point は Script Asset と独立して必要なため、空 Project でも先に配置する。
        Result result = m_fileSystem->create_directories(
            Core::IO::Path::join(a_projectRoot, Core::IO::Path("EngineModule")));
        if (!result)
        {
            return result;
        }

        // CMake 構成と module entry point を同じ root に揃え、外部 IDE から Project 単位で開けるようにする。
        result = ensure_text_file(
            Core::IO::Path::join(a_projectRoot, Core::IO::Path("CMakeLists.txt")),
            k_cmakeListsText);
        if (!result)
        {
            return result;
        }

        result = ensure_text_file(
            Core::IO::Path::join(a_projectRoot, Core::IO::Path("CMakePresets.json")),
            k_cmakePresetsText);
        if (!result)
        {
            return result;
        }

        return ensure_text_file(
            Core::IO::Path::join(a_projectRoot, Core::IO::Path("EngineModule/GameScriptModule.cpp")),
            k_moduleText);
    }

    Result GameProjectGenerator::ensure_text_file(
        const Core::IO::Path& a_path, std::string_view a_text) const
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Game project file system is not initialized.");
        }

        bool exists = false;
        Result result = m_fileSystem->exists(a_path, &exists);
        if (!result)
        {
            return result;
        }
        if (exists)
        {
            return Result::ok();
        }

        const std::vector<std::byte> data = to_file_data(a_text);
        return m_fileSystem->write_all(a_path, data, true);
    }
} // namespace Cue::Editor
