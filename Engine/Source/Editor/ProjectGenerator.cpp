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
                    { "cacheVariables", {
                        { "CMAKE_TOOLCHAIN_FILE",
                            "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" }
                    } },
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

        const std::string gameScriptModuleText =
            "#include <Native/ScriptAbi.h>\n"
            "\n"
            "// === C++ includes ===\n"
            "#include <cstddef>\n"
            "#include <cstring>\n"
            "#include <string_view>\n"
            "#include <unordered_map>\n"
            "\n"
            "namespace\n"
            "{\n"
            "    struct ScriptInstance final\n"
            "    {\n"
            "        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };\n"
            "        float elapsedSeconds = 0.0f;\n"
            "    };\n"
            "\n"
            "    const CueEngineApi* g_engineApi = nullptr;\n"
            "    std::unordered_map<uint64_t, ScriptInstance> g_instances{};\n"
            "    uint64_t g_nextInstanceId = 1;\n"
            "\n"
            "    inline constexpr uint32_t k_requiredEngineApiSize =\n"
            "        static_cast<uint32_t>(\n"
            "            offsetof(CueEngineApi, setTransform) + sizeof(CueSetTransformFn));\n"
            "\n"
            "    [[nodiscard]] bool supports_register_script_class(\n"
            "        const CueEngineApi* a_engineApi)\n"
            "    {\n"
            "        return a_engineApi != nullptr &&\n"
            "            a_engineApi->structSize >=\n"
            "            offsetof(CueEngineApi, registerScriptClass) +\n"
            "                sizeof(CueRegisterScriptClassFn) &&\n"
            "            a_engineApi->registerScriptClass != nullptr;\n"
            "    }\n"
            "\n"
            "    [[nodiscard]] CueResult validate_engine_api(const CueEngineApi* a_engineApi)\n"
            "    {\n"
            "        if (a_engineApi == nullptr)\n"
            "        {\n"
            "            return CueResult_InvalidArgument;\n"
            "        }\n"
            "        if (a_engineApi->structSize < k_requiredEngineApiSize)\n"
            "        {\n"
            "            return CueResult_InvalidArgument;\n"
            "        }\n"
            "        if (a_engineApi->abiVersion != k_cueScriptAbiVersion)\n"
            "        {\n"
            "            return CueResult_Unsupported;\n"
            "        }\n"
            "        if (a_engineApi->log == nullptr ||\n"
            "            a_engineApi->isEntityValid == nullptr ||\n"
            "            a_engineApi->hasTransform == nullptr ||\n"
            "            a_engineApi->getTransform == nullptr ||\n"
            "            a_engineApi->setTransform == nullptr)\n"
            "        {\n"
            "            return CueResult_InvalidArgument;\n"
            "        }\n"
            "\n"
            "        return CueResult_Ok;\n"
            "    }\n"
            "\n"
            "    [[nodiscard]] CueStringView make_string_view(std::string_view a_text)\n"
            "    {\n"
            "        return CueStringView{\n"
            "            a_text.data(),\n"
            "            static_cast<uint32_t>(a_text.size())\n"
            "        };\n"
            "    }\n"
            "\n"
            "    void log_message(CueLogSeverity a_severity, std::string_view a_message)\n"
            "    {\n"
            "        if (g_engineApi == nullptr || g_engineApi->log == nullptr)\n"
            "        {\n"
            "            return;\n"
            "        }\n"
            "\n"
            "        (void)g_engineApi->log(a_severity, make_string_view(a_message));\n"
            "    }\n"
            "\n"
            "    [[nodiscard]] bool string_view_equals(\n"
            "        CueStringView a_left, std::string_view a_right)\n"
            "    {\n"
            "        if (a_left.data == nullptr)\n"
            "        {\n"
            "            return false;\n"
            "        }\n"
            "        if (a_left.size != a_right.size())\n"
            "        {\n"
            "            return false;\n"
            "        }\n"
            "\n"
            "        return std::memcmp(a_left.data, a_right.data(), a_right.size()) == 0;\n"
            "    }\n"
            "}\n"
            "\n"
            "extern \"C\"\n"
            "{\n"
            "    CueScriptAbiVersion CUE_SCRIPT_CALL cue_script_get_abi_version(void)\n"
            "    {\n"
            "        return k_cueScriptAbiVersion;\n"
            "    }\n"
            "\n"
            "    CueResult CUE_SCRIPT_CALL cue_script_get_exports(\n"
            "        CueScriptExports* a_outExports)\n"
            "    {\n"
            "        if (a_outExports == nullptr)\n"
            "        {\n"
            "            return CueResult_InvalidArgument;\n"
            "        }\n"
            "\n"
            "        a_outExports->structSize = sizeof(CueScriptExports);\n"
            "        a_outExports->abiVersion = k_cueScriptAbiVersion;\n"
            "        a_outExports->registerScripts =\n"
            "            [](const CueEngineApi* a_engineApi) -> CueResult\n"
            "            {\n"
            "                const CueResult result = validate_engine_api(a_engineApi);\n"
            "                if (result != CueResult_Ok)\n"
            "                {\n"
            "                    return result;\n"
            "                }\n"
            "\n"
            "                g_engineApi = a_engineApi;\n"
            "                if (supports_register_script_class(g_engineApi))\n"
            "                {\n"
            "                    const CueResult registerResult =\n"
            "                        g_engineApi->registerScriptClass(\n"
            "                            make_string_view(\"RotateCube\"));\n"
            "                    if (registerResult != CueResult_Ok)\n"
            "                    {\n"
            "                        return registerResult;\n"
            "                    }\n"
            "                }\n"
            "\n"
            "                log_message(CueLogSeverity_Info,\n"
            "                    \"GameScript module registered.\");\n"
            "                return CueResult_Ok;\n"
            "            };\n"
            "        a_outExports->createScriptInstance =\n"
            "            [](const CueScriptCreateInfo* a_createInfo,\n"
            "                CueScriptInstanceHandle* a_outInstanceHandle) -> CueResult\n"
            "            {\n"
            "                if (g_engineApi == nullptr)\n"
            "                {\n"
            "                    return CueResult_InvalidState;\n"
            "                }\n"
            "                if (a_createInfo == nullptr || a_outInstanceHandle == nullptr)\n"
            "                {\n"
            "                    return CueResult_InvalidArgument;\n"
            "                }\n"
            "                if (a_createInfo->entityHandle.value == k_cueInvalidHandleValue)\n"
            "                {\n"
            "                    return CueResult_InvalidArgument;\n"
            "                }\n"
            "                if (g_engineApi->isEntityValid(a_createInfo->entityHandle) == 0)\n"
            "                {\n"
            "                    return CueResult_NotFound;\n"
            "                }\n"
            "                if (!string_view_equals(a_createInfo->scriptName, \"RotateCube\"))\n"
            "                {\n"
            "                    return CueResult_NotFound;\n"
            "                }\n"
            "\n"
            "                const uint64_t instanceId = g_nextInstanceId++;\n"
            "                g_instances.emplace(instanceId, ScriptInstance{\n"
            "                    a_createInfo->entityHandle,\n"
            "                    0.0f\n"
            "                });\n"
            "                a_outInstanceHandle->value = instanceId;\n"
            "                return CueResult_Ok;\n"
            "            };\n"
            "        a_outExports->destroyScriptInstance =\n"
            "            [](CueScriptInstanceHandle a_instanceHandle) -> CueResult\n"
            "            {\n"
            "                if (a_instanceHandle.value == k_cueInvalidHandleValue)\n"
            "                {\n"
            "                    return CueResult_InvalidArgument;\n"
            "                }\n"
            "\n"
            "                const size_t erased = g_instances.erase(a_instanceHandle.value);\n"
            "                return erased > 0 ? CueResult_Ok : CueResult_NotFound;\n"
            "            };\n"
            "        a_outExports->updateScriptInstance =\n"
            "            [](CueScriptInstanceHandle a_instanceHandle,\n"
            "                float a_deltaTimeSeconds) -> CueResult\n"
            "            {\n"
            "                if (g_engineApi == nullptr)\n"
            "                {\n"
            "                    return CueResult_InvalidState;\n"
            "                }\n"
            "                if (a_instanceHandle.value == k_cueInvalidHandleValue)\n"
            "                {\n"
            "                    return CueResult_InvalidArgument;\n"
            "                }\n"
            "\n"
            "                const auto instanceIt = g_instances.find(a_instanceHandle.value);\n"
            "                if (instanceIt == g_instances.end())\n"
            "                {\n"
            "                    return CueResult_NotFound;\n"
            "                }\n"
            "\n"
            "                ScriptInstance& instance = instanceIt->second;\n"
            "                if (g_engineApi->isEntityValid(instance.entityHandle) == 0)\n"
            "                {\n"
            "                    return CueResult_NotFound;\n"
            "                }\n"
            "                if (g_engineApi->hasTransform(instance.entityHandle) == 0)\n"
            "                {\n"
            "                    return CueResult_NotFound;\n"
            "                }\n"
            "\n"
            "                CueTransformData transform{};\n"
            "                CueResult result = g_engineApi->getTransform(\n"
            "                    instance.entityHandle, &transform);\n"
            "                if (result != CueResult_Ok)\n"
            "                {\n"
            "                    return result;\n"
            "                }\n"
            "\n"
            "                instance.elapsedSeconds += a_deltaTimeSeconds;\n"
            "                transform.rotation.y += a_deltaTimeSeconds * 45.0f;\n"
            "\n"
            "                result = g_engineApi->setTransform(\n"
            "                    instance.entityHandle, &transform);\n"
            "                if (result != CueResult_Ok)\n"
            "                {\n"
            "                    return result;\n"
            "                }\n"
            "\n"
            "                return CueResult_Ok;\n"
            "            };\n"
            "\n"
            "        return CueResult_Ok;\n"
            "    }\n"
            "}\n";

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
