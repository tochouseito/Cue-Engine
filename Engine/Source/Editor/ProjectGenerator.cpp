#include "ProjectGenerator.h"

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/SceneSerializer.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cctype>
#include <span>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] GameCore::ObjectDefinition make_default_camera_object()
        {
            GameCore::ObjectDefinition camera("MainCamera");

            ECS::TransformComponent transform{};
            transform.position = Math::float3(0.0f, 0.0f, -6.0f);
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            camera.prototype.add_component(transform);

            ECS::CameraComponent cameraComponent{};
            cameraComponent.isMain = true;
            cameraComponent.fovY = 60.0f;
            cameraComponent.aspectRatio = 16.0f / 9.0f;
            cameraComponent.nearZ = 0.1f;
            cameraComponent.farZ = 1000.0f;
            camera.prototype.add_component(cameraComponent);

            return camera;
        }

        [[nodiscard]] GameCore::ObjectDefinition make_default_cube_object()
        {
            GameCore::ObjectDefinition cube("Cube");

            ECS::TransformComponent transform{};
            transform.position = Math::float3::zero();
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            cube.prototype.add_component(transform);

            ECS::MeshFilterComponent meshFilter{};
            meshFilter.meshId = 0;
            cube.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.materialId = 0;
            renderer.visible = true;
            cube.prototype.add_component(renderer);

            ECS::ScriptComponent script{};
            script.className = "RotateCube";
            script.isEnabled = true;
            cube.prototype.add_component(script);

            return cube;
        }
    }

    ProjectGenerator::ProjectGenerator(
        Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(a_fileSystem)
    {
    }

    Result ProjectGenerator::generate(
        const ProjectGenerationRequest& a_request,
        std::string& a_outProjectPath)
    {
        a_outProjectPath.clear();

        if (a_request.projectName.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "プロジェクト名を入力してください。");
        }

        if (a_request.baseDirectory.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "作成先ディレクトリを指定してください。");
        }

        if (has_invalid_project_name_character(a_request.projectName))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "プロジェクト名に使用できない文字が含まれています。");
        }

        const Core::IO::Path basePath(a_request.baseDirectory);
        bool baseExists = false;
        Result result = m_fileSystem.exists(basePath, &baseExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "作成先ディレクトリの確認に失敗しました。");
        }

        if (!baseExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "作成先ディレクトリが存在しないか、ディレクトリではありません。");
        }

        Core::IO::FileStat baseStat{};
        result = m_fileSystem.stat(basePath, &baseStat);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "作成先ディレクトリの情報取得に失敗しました。");
        }

        if (baseStat.type != Core::IO::FileType::directory)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "作成先ディレクトリが存在しないか、ディレクトリではありません。");
        }

        const Core::IO::Path projectPath =
            Core::IO::Path::join(basePath, Core::IO::Path(a_request.projectName));
        bool projectExists = false;
        result = m_fileSystem.exists(projectPath, &projectExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "作成先プロジェクトパスの確認に失敗しました。");
        }

        if (projectExists)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "同名のフォルダがすでに存在します。");
        }

        result = m_fileSystem.create_directories(projectPath);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "プロジェクトフォルダの作成に失敗しました。");
        }

        result = create_project_directories(projectPath);
        if (!result)
        {
            return result;
        }

        result = write_script_project_files(a_request.projectName, projectPath);
        if (!result)
        {
            return result;
        }

        result = write_default_scene(projectPath);
        if (!result)
        {
            return result;
        }

        result = write_project_file(a_request.projectName, projectPath);
        if (!result)
        {
            return result;
        }

        a_outProjectPath = projectPath.utf8();
        return Result::ok();
    }

    Result ProjectGenerator::create_project_directories(
        const Core::IO::Path& a_projectPath)
    {
        const std::array<Core::IO::Path, 6> directories = {
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Assets")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Assets/Scenes")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("GameScript")),
            Core::IO::Path::join(
                a_projectPath, Core::IO::Path("GameScript/Source")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Saved")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Intermediate")),
        };

        for (const Core::IO::Path& directoryPath : directories)
        {
            const Result result = m_fileSystem.create_directories(directoryPath);
            if (!result)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "プロジェクト初期フォルダの作成に失敗しました。");
            }
        }

        return Result::ok();
    }

    Result ProjectGenerator::write_script_project_files(
        const std::string& a_projectName,
        const Core::IO::Path& a_projectPath)
    {
        const std::string cmakeProjectName = make_cmake_project_name(a_projectName);
        const std::string engineRoot = CUE_PROJECT_ROOT_PATH;

        const std::string rootCMakeText =
            "cmake_minimum_required(VERSION 4.2.0)\n"
            "\n"
            "project(" + cmakeProjectName + "Scripts VERSION 0.0.1 LANGUAGES C CXX)\n"
            "\n"
            "set(CMAKE_CXX_STANDARD 20)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_CXX_EXTENSIONS OFF)\n"
            "\n"
            "add_subdirectory(\"GameScript\")\n";

        nlohmann::json presetsJson = {
            { "version", 6 },
            { "configurePresets", nlohmann::json::array({
                {
                    { "name", "windows-base" },
                    { "hidden", true },
                    { "generator", "Visual Studio 18 2026" },
                    { "binaryDir", "${sourceDir}/out/build/${presetName}" },
                    { "installDir", "${sourceDir}/out/install/${presetName}" },
                    { "condition", {
                        { "type", "equals" },
                        { "lhs", "${hostSystemName}" },
                        { "rhs", "Windows" }
                    } }
                },
                {
                    { "name", "win-x64" },
                    { "displayName", "Windows x64 (VS/MSBuild)" },
                    { "inherits", "windows-base" },
                    { "architecture", {
                        { "value", "x64" },
                        { "strategy", "set" }
                    } }
                }
            }) },
            { "buildPresets", nlohmann::json::array({
                {
                    { "name", "win-x64-debug" },
                    { "displayName", "Windows x64 Debug" },
                    { "configurePreset", "win-x64" },
                    { "configuration", "Debug" }
                },
                {
                    { "name", "win-x64-relwithdebinfo" },
                    { "displayName", "Windows x64 RelWithDebInfo" },
                    { "configurePreset", "win-x64" },
                    { "configuration", "RelWithDebInfo" }
                },
                {
                    { "name", "win-x64-release" },
                    { "displayName", "Windows x64 Release" },
                    { "configurePreset", "win-x64" },
                    { "configuration", "Release" }
                }
            }) }
        };

        std::string presetsText = presetsJson.dump(4);
        presetsText.push_back('\n');

        const std::string gameScriptCMakeText =
            "# GameScript module\n"
            "\n"
            "set(CUE_ENGINE_ROOT \"" + engineRoot + "\")\n"
            "\n"
            "if(NOT EXISTS \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h\")\n"
            "    message(FATAL_ERROR \"Cue Engine root not found: ${CUE_ENGINE_ROOT}\")\n"
            "endif()\n"
            "\n"
            "add_library(GameScript SHARED\n"
            "    \"Source/GameScriptModule.cpp\"\n"
            ")\n"
            "\n"
            "target_include_directories(GameScript PRIVATE\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core\"\n"
            ")\n"
            "\n"
            "target_compile_definitions(GameScript PRIVATE\n"
            "    CUE_SCRIPT_DLL_EXPORTS=1\n"
            ")\n"
            "\n"
            "if(MSVC)\n"
            "    target_compile_options(GameScript PRIVATE\n"
            "        /utf-8\n"
            "        /W4\n"
            "        /EHsc\n"
            "        /wd4201\n"
            "    )\n"
            "endif()\n"
            "\n"
            "set_target_properties(GameScript PROPERTIES\n"
            "    OUTPUT_NAME \"GameScript\"\n"
            ")\n";

        const std::string gameScriptModuleText = R"(#include <Native/ScriptAbi.h>
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <string_view>

namespace
{
    using Cue::Core::Native::ScriptClassDefinition;
    using Cue::Core::Native::ScriptModuleRuntime;
    using Cue::Core::Native::make_script_string_view;
    using Cue::Core::Native::string_view_equals;

    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;
    inline constexpr uint32_t k_stateVersion = 1u;

    struct RotateCubeState final
    {
        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;
    };

    struct RotateCubeStateBlob final
    {
        uint32_t version = k_stateVersion;
        float elapsedSeconds = 0.0f;
        float rotationSpeed = k_rotationSpeedRadiansPerSecond;
    };

    inline constexpr uint32_t k_requiredEngineApiSize =
        static_cast<uint32_t>(
            offsetof(CueEngineApi, setTransform) + sizeof(CueSetTransformFn));

    [[nodiscard]] CueResult validate_engine_api(
        const CueEngineApi* a_engineApi)
    {
        if (a_engineApi == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_engineApi->structSize < k_requiredEngineApiSize)
        {
            return CueResult_InvalidArgument;
        }
        if (a_engineApi->abiVersion != k_cueScriptAbiVersion)
        {
            return CueResult_Unsupported;
        }
        if (a_engineApi->log == nullptr ||
            a_engineApi->isEntityValid == nullptr ||
            a_engineApi->hasTransform == nullptr ||
            a_engineApi->getTransform == nullptr ||
            a_engineApi->setTransform == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        return CueResult_Ok;
    }

    void log_message(const CueEngineApi* a_engineApi,
        CueLogSeverity a_severity,
        std::string_view a_message)
    {
        if (a_engineApi == nullptr || a_engineApi->log == nullptr)
        {
            return;
        }

        (void)a_engineApi->log(a_severity,
            make_script_string_view(
                a_message.data(),
                static_cast<uint32_t>(a_message.size())));
    }

    [[nodiscard]] const CueScriptFieldValue* find_field_value(
        const CueScriptCreateInfo* a_createInfo,
        CueStringView a_fieldName)
    {
        if (a_createInfo == nullptr ||
            a_createInfo->fieldValues == nullptr ||
            a_createInfo->fieldValueCount == 0)
        {
            return nullptr;
        }

        for (uint32_t fieldIndex = 0;
             fieldIndex < a_createInfo->fieldValueCount;
             ++fieldIndex)
        {
            const CueScriptFieldValue& fieldValue =
                a_createInfo->fieldValues[fieldIndex];
            if (string_view_equals(fieldValue.name, a_fieldName))
            {
                return &fieldValue;
            }
        }

        return nullptr;
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL create_rotate_cube_state(
        const CueEngineApi*,
        const CueScriptCreateInfo* a_createInfo,
        void** a_outState)
    {
        if (a_createInfo == nullptr || a_outState == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        auto* state = new (std::nothrow) RotateCubeState();
        if (state == nullptr)
        {
            return CueResult_InternalError;
        }

        state->entityHandle = a_createInfo->entityHandle;
        const CueScriptFieldValue* rotationSpeedField =
            find_field_value(
                a_createInfo, CUE_SCRIPT_STRING_VIEW("rotationSpeed"));
        if (rotationSpeedField != nullptr &&
            rotationSpeedField->type == CueScriptFieldType_Float)
        {
            state->rotationSpeed = rotationSpeedField->floatValue;
        }

        *a_outState = state;
        return CueResult_Ok;
    }

    void CUE_SCRIPT_CALL destroy_rotate_cube_state(void* a_state)
    {
        delete static_cast<RotateCubeState*>(a_state);
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL update_rotate_cube_state(
        const CueEngineApi* a_engineApi,
        void* a_state,
        float a_deltaTimeSeconds)
    {
        if (a_engineApi == nullptr || a_state == nullptr)
        {
            return CueResult_InvalidState;
        }

        auto* state = static_cast<RotateCubeState*>(a_state);
        if (a_engineApi->isEntityValid(state->entityHandle) == 0)
        {
            return CueResult_NotFound;
        }
        if (a_engineApi->hasTransform(state->entityHandle) == 0)
        {
            return CueResult_NotFound;
        }

        CueTransformData transform{};
        CueResult result =
            a_engineApi->getTransform(state->entityHandle, &transform);
        if (result != CueResult_Ok)
        {
            return result;
        }

        state->elapsedSeconds += a_deltaTimeSeconds;
        transform.rotation.y += a_deltaTimeSeconds * state->rotationSpeed;
        return a_engineApi->setTransform(state->entityHandle, &transform);
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL serialize_rotate_cube_state(
        const void* a_state,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_outStateBuffer == nullptr ||
            a_stateBufferSize != sizeof(RotateCubeStateBlob))
        {
            return CueResult_InvalidArgument;
        }

        const auto* state = static_cast<const RotateCubeState*>(a_state);
        RotateCubeStateBlob blob{};
        blob.elapsedSeconds = state->elapsedSeconds;
        blob.rotationSpeed = state->rotationSpeed;
        std::memcpy(a_outStateBuffer, &blob, sizeof(blob));
        return CueResult_Ok;
    }

    [[nodiscard]] CueResult CUE_SCRIPT_CALL restore_rotate_cube_state(
        void* a_state,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_stateBuffer == nullptr ||
            a_stateBufferSize != sizeof(RotateCubeStateBlob))
        {
            return CueResult_InvalidArgument;
        }

        RotateCubeStateBlob blob{};
        std::memcpy(&blob, a_stateBuffer, sizeof(blob));
        if (blob.version != k_stateVersion)
        {
            return CueResult_Unsupported;
        }

        auto* state = static_cast<RotateCubeState*>(a_state);
        state->elapsedSeconds = blob.elapsedSeconds;
        state->rotationSpeed = blob.rotationSpeed;
        return CueResult_Ok;
    }

    inline constexpr std::array<CueScriptFieldValue, 1> k_rotateCubeFields = {
        CUE_FIELD_FLOAT("rotationSpeed", k_rotationSpeedRadiansPerSecond)
    };

    inline constexpr ScriptClassDefinition k_rotateCubeScript = CUE_SCRIPT(
        "RotateCube",
        k_stateVersion,
        RotateCubeStateBlob,
        "RotateCube:v1:elapsedSeconds:f32;rotationSpeed:f32",
        k_rotateCubeFields,
        &create_rotate_cube_state,
        &destroy_rotate_cube_state,
        &update_rotate_cube_state,
        &serialize_rotate_cube_state,
        &restore_rotate_cube_state);

    inline constexpr std::array<ScriptClassDefinition, 1> k_scriptClasses = {
        k_rotateCubeScript
    };

    [[nodiscard]] std::span<const ScriptClassDefinition> script_classes() noexcept
    {
        return std::span<const ScriptClassDefinition>(
            k_scriptClasses.data(), k_scriptClasses.size());
    }

    ScriptModuleRuntime g_scriptRuntime{};
}

extern "C"
{
    CueScriptAbiVersion CUE_SCRIPT_CALL cue_script_get_abi_version(void)
    {
        return k_cueScriptAbiVersion;
    }

    CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports)
    {
        if (a_outExports == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        a_outExports->structSize = sizeof(CueScriptExports);
        a_outExports->abiVersion = k_cueScriptAbiVersion;
        a_outExports->registerScripts =
            [](const CueEngineApi* a_engineApi) -> CueResult
            {
                const CueResult result = validate_engine_api(a_engineApi);
                if (result != CueResult_Ok)
                {
                    return result;
                }

                const CueResult registerResult =
                    g_scriptRuntime.register_scripts(
                        a_engineApi, script_classes());
                if (registerResult != CueResult_Ok)
                {
                    return registerResult;
                }

                log_message(a_engineApi, CueLogSeverity_Info,
                    "GameScript module registered.");
                return CueResult_Ok;
            };
        a_outExports->createScriptInstance =
            [](const CueScriptCreateInfo* a_createInfo,
                CueScriptInstanceHandle* a_outInstanceHandle) -> CueResult
            {
                return g_scriptRuntime.create_script_instance(
                    script_classes(), a_createInfo, a_outInstanceHandle);
            };
        a_outExports->destroyScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle) -> CueResult
            {
                return g_scriptRuntime.destroy_script_instance(a_instanceHandle);
            };
        a_outExports->updateScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                float a_deltaTimeSeconds) -> CueResult
            {
                return g_scriptRuntime.update_script_instance(
                    a_instanceHandle, a_deltaTimeSeconds);
            };
        a_outExports->getScriptInstanceStateSize =
            [](CueScriptInstanceHandle a_instanceHandle,
                uint32_t* a_outStateSize) -> CueResult
            {
                return g_scriptRuntime.get_script_instance_state_size(
                    a_instanceHandle, a_outStateSize);
            };
        a_outExports->serializeScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                void* a_outStateBuffer,
                uint32_t a_stateBufferSize) -> CueResult
            {
                return g_scriptRuntime.serialize_script_instance(
                    a_instanceHandle, a_outStateBuffer, a_stateBufferSize);
            };
        a_outExports->restoreScriptInstance =
            [](CueScriptInstanceHandle a_instanceHandle,
                const void* a_stateBuffer,
                uint32_t a_stateBufferSize) -> CueResult
            {
                return g_scriptRuntime.restore_script_instance(
                    a_instanceHandle, a_stateBuffer, a_stateBufferSize);
            };
        a_outExports->getScriptStateDescriptor =
            [](CueStringView a_scriptClassName,
                CueScriptStateDescriptor* a_outDescriptor) -> CueResult
            {
                return g_scriptRuntime.get_script_state_descriptor(
                    script_classes(), a_scriptClassName, a_outDescriptor);
            };
        return CueResult_Ok;
    }
}
)";

        Result result = write_text_file(
            Core::IO::Path::join(a_projectPath, Core::IO::Path("CMakeLists.txt")),
            rootCMakeText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "スクリプト用 CMakeLists.txt の作成に失敗しました。");
        }

        result = write_text_file(
            Core::IO::Path::join(a_projectPath, Core::IO::Path("CMakePresets.json")),
            presetsText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "スクリプト用 CMakePresets.json の作成に失敗しました。");
        }

        result = write_text_file(
            Core::IO::Path::join(
                a_projectPath, Core::IO::Path("GameScript/CMakeLists.txt")),
            gameScriptCMakeText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "GameScript/CMakeLists.txt の作成に失敗しました。");
        }

        result = write_text_file(
            Core::IO::Path::join(
                a_projectPath,
                Core::IO::Path("GameScript/Source/GameScriptModule.cpp")),
            gameScriptModuleText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "GameScriptModule.cpp の作成に失敗しました。");
        }

        return Result::ok();
    }

    Result ProjectGenerator::write_default_scene(
        const Core::IO::Path& a_projectPath)
    {
        GameCore::SceneAsset sceneAsset("Main");
        sceneAsset.add_object(make_default_camera_object());
        sceneAsset.add_object(make_default_cube_object());

        const Core::IO::Path scenePath = Core::IO::Path::join(
            a_projectPath,
            Core::IO::Path("Assets/Scenes/Main.cuescene"));
        const Result result = GameCore::SceneSerializer::save_scene_asset(
            sceneAsset, m_fileSystem, scenePath);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Main.cuescene の作成に失敗しました。");
        }

        return Result::ok();
    }

    Result ProjectGenerator::write_project_file(
        const std::string& a_projectName,
        const Core::IO::Path& a_projectPath)
    {
        nlohmann::json projectJson = {
            { "name", a_projectName },
            { "engineVersion", 1 },
            { "assetRoot", "Assets" },
            { "scriptRoot", "." },
            { "scriptBuildConfiguration", "Debug" },
            { "scriptBuildBackend", "CMake" },
            { "startupScene", "Assets/Scenes/Main.cuescene" }
        };

        std::string jsonText = projectJson.dump(4);
        jsonText.push_back('\n');

        const Core::IO::Path projectFilePath = Core::IO::Path::join(
            a_projectPath,
            Core::IO::Path("cueproject.json"));

        const std::span<const char> textSpan(jsonText.data(), jsonText.size());
        const std::span<const std::byte> byteSpan = std::as_bytes(textSpan);
        const Result result =
            m_fileSystem.write_all(projectFilePath, byteSpan, false);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "cueproject.json の書き込みに失敗しました。");
        }

        return Result::ok();
    }

    Result ProjectGenerator::write_text_file(
        const Core::IO::Path& a_filePath,
        const std::string& a_text)
    {
        const std::span<const char> textSpan(a_text.data(), a_text.size());
        const std::span<const std::byte> byteSpan = std::as_bytes(textSpan);
        return m_fileSystem.write_all(a_filePath, byteSpan, false);
    }

    bool ProjectGenerator::has_invalid_project_name_character(
        const std::string& a_projectName) const
    {
        static constexpr const char* k_invalidChars = "\\/:*?\"<>|";
        return a_projectName.find_first_of(k_invalidChars) != std::string::npos;
    }

    std::string ProjectGenerator::make_cmake_project_name(
        const std::string& a_projectName) const
    {
        std::string result{};
        result.reserve(a_projectName.size() + 1);

        for (const unsigned char ch : a_projectName)
        {
            if (std::isalnum(ch) != 0)
            {
                result.push_back(static_cast<char>(ch));
            }
            else
            {
                result.push_back('_');
            }
        }

        if (result.empty())
        {
            return "CueProject";
        }

        if (std::isdigit(static_cast<unsigned char>(result.front())) != 0)
        {
            result.insert(result.begin(), '_');
        }

        return result;
    }
}
