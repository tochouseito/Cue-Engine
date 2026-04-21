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
#include <vector>

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

    Result ProjectGenerator::create_script_template(
        const std::string& a_projectPath,
        const std::string& a_scriptName)
    {
        if (a_projectPath.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "プロジェクトパスが未設定です。");
        }
        if (a_scriptName.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script 名を入力してください。");
        }
        if (has_invalid_script_name_character(a_scriptName))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script 名には英数字と _ のみ使用できます。");
        }
        if (std::isdigit(static_cast<unsigned char>(a_scriptName.front())) != 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script 名の先頭に数字は使用できません。");
        }

        const std::string symbolName = make_script_symbol_name(a_scriptName);
        const Core::IO::Path projectPath(a_projectPath);
        const Core::IO::Path headerPath = Core::IO::Path::join(
            projectPath,
            Core::IO::Path("Assets/Scripts/" + a_scriptName + "Script.h"));
        const Core::IO::Path sourcePath = Core::IO::Path::join(
            projectPath,
            Core::IO::Path("Assets/Scripts/" + a_scriptName + "Script.cpp"));

        bool exists = false;
        Result result = m_fileSystem.exists(headerPath, &exists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script ヘッダの存在確認に失敗しました。");
        }
        if (exists)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "同名の Script ヘッダが既に存在します。");
        }

        result = m_fileSystem.exists(sourcePath, &exists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script ソースの存在確認に失敗しました。");
        }
        if (exists)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "同名の Script ソースが既に存在します。");
        }

        const std::string headerText =
            "#pragma once\n"
            "\n"
            "#include <ScriptFramework/Marionette.h>\n"
            "\n"
            "MARIONETTE_DECLARE_SCRIPT_TYPE(" + a_scriptName + ", \"" + a_scriptName + "\");\n"
            "\n"
            "class " + a_scriptName + " final : public Marionette::Behaviour<" + a_scriptName + ">\n"
            "{\n"
            "public:\n"
            "    using StateBlob = Marionette::StateBlob<" + a_scriptName + ">;\n"
            "    using Marionette::Behaviour<" + a_scriptName + ">::update;\n"
            "    MARIONETTE_NO_FIELDS();\n"
            "    MARIONETTE_NO_FUNCTIONS();\n"
            "\n"
            "    void start();\n"
            "    void update();\n"
            "};\n"
            "\n"
            "[[nodiscard]] Cue::Core::Native::ScriptClassDefinition\n"
            "make_" + symbolName + "_script_definition() noexcept;\n";

        const std::string sourceText =
            "#include \"" + a_scriptName + "Script.h\"\n"
            "\n"
            "void " + a_scriptName + "::start()\n"
            "{\n"
            "}\n"
            "\n"
            "void " + a_scriptName + "::update()\n"
            "{\n"
            "}\n"
            "\n"
            "MARIONETTE_DEFINE_SCRIPT(" + symbolName + ", " + a_scriptName + ");\n";

        result = write_text_file(headerPath, headerText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Script ヘッダの作成に失敗しました。");
        }

        result = write_text_file(sourcePath, sourceText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Script ソースの作成に失敗しました。");
        }

        return Result::ok();
    }

    Result ProjectGenerator::create_project_directories(
        const Core::IO::Path& a_projectPath)
    {
        const std::array<Core::IO::Path, 6> directories = {
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Assets")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Assets/Scenes")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("Assets/Scripts")),
            Core::IO::Path::join(a_projectPath, Core::IO::Path("EngineModule")),
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
            "set(CUE_ENGINE_ROOT \"" + engineRoot + "\")\n"
            "\n"
            "if(NOT EXISTS \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h\")\n"
            "    message(FATAL_ERROR \"Cue Engine root not found: ${CUE_ENGINE_ROOT}\")\n"
            "endif()\n"
            "\n"
            "file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.cpp\"\n"
            ")\n"
            "\n"
            "file(GLOB_RECURSE GAME_SCRIPT_HEADERS CONFIGURE_DEPENDS\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.h\"\n"
            ")\n"
            "\n"
            "set(CUE_SCRIPT_GENERATED_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/Generated\")\n"
            "set(CUE_SCRIPT_REGISTRY_CPP \"${CUE_SCRIPT_GENERATED_DIR}/ScriptRegistry.gen.cpp\")\n"
            "\n"
            "add_custom_command(\n"
            "    OUTPUT \"${CUE_SCRIPT_REGISTRY_CPP}\"\n"
            "    COMMAND ${CMAKE_COMMAND} -E make_directory \"${CUE_SCRIPT_GENERATED_DIR}\"\n"
            "    COMMAND ${CMAKE_COMMAND}\n"
            "        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}\n"
            "        -DASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Assets\n"
            "        -DSCRIPT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts\n"
            "        -DOUTPUT=${CUE_SCRIPT_REGISTRY_CPP}\n"
            "        -P \"${CUE_ENGINE_ROOT}/Tools/CMake/GenerateScriptRegistry.cmake\"\n"
            "    DEPENDS\n"
            "        ${GAME_SCRIPT_HEADERS}\n"
            "        \"${CMAKE_CURRENT_SOURCE_DIR}/EngineModule/ScriptRegistry.h\"\n"
            "        \"${CUE_ENGINE_ROOT}/Tools/CMake/GenerateScriptRegistry.cmake\"\n"
            "    VERBATIM\n"
            ")\n"
            "\n"
            "add_library(GameScript SHARED\n"
            "    \"EngineModule/ScriptRegistry.h\"\n"
            "    ${GAME_SCRIPT_SOURCES}\n"
            "    \"${CUE_SCRIPT_REGISTRY_CPP}\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/ScriptFramework/GameScriptModule.cpp\"\n"
            ")\n"
            "\n"
            "target_include_directories(GameScript PRIVATE\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/EngineModule\"\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/Assets\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine\"\n"
            "    \"${CUE_SCRIPT_GENERATED_DIR}\"\n"
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

        Result result = Result::ok();
        const std::string scriptRegistryHeaderText = R"(#pragma once

#include <Native/ScriptModuleRuntime.h>

// *** Script classes registered in this module
[[nodiscard]] std::span<const Cue::Core::Native::ScriptClassDefinition>
script_classes() noexcept;
)";

        result = write_text_file(
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
                a_projectPath, Core::IO::Path("EngineModule/ScriptRegistry.h")),
            scriptRegistryHeaderText);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "ScriptRegistry.h の作成に失敗しました。");
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

    Result ProjectGenerator::read_text_file(
        const Core::IO::Path& a_filePath,
        std::string& a_outText)
    {
        a_outText.clear();

        bool exists = false;
        Result result = m_fileSystem.exists(a_filePath, &exists);
        if (!result)
        {
            return result;
        }
        if (!exists)
        {
            return Result::ok();
        }

        std::vector<std::byte> data{};
        result = m_fileSystem.read_all(a_filePath, &data);
        if (!result)
        {
            return result;
        }

        a_outText.assign(
            reinterpret_cast<const char*>(data.data()),
            data.size());
        return Result::ok();
    }

    bool ProjectGenerator::has_invalid_project_name_character(
        const std::string& a_projectName) const
    {
        static constexpr const char* k_invalidChars = "\\/:*?\"<>|";
        return a_projectName.find_first_of(k_invalidChars) != std::string::npos;
    }

    bool ProjectGenerator::has_invalid_script_name_character(
        const std::string& a_scriptName) const
    {
        for (const unsigned char ch : a_scriptName)
        {
            if (std::isalnum(ch) == 0 && ch != '_')
            {
                return true;
            }
        }

        return false;
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

    std::string ProjectGenerator::make_script_symbol_name(
        const std::string& a_scriptName) const
    {
        std::string result{};
        result.reserve(a_scriptName.size() * 2);

        for (size_t i = 0; i < a_scriptName.size(); ++i)
        {
            const unsigned char ch =
                static_cast<unsigned char>(a_scriptName[i]);
            if (std::isupper(ch) != 0)
            {
                if (!result.empty() && result.back() != '_')
                {
                    result.push_back('_');
                }
                result.push_back(
                    static_cast<char>(std::tolower(ch)));
            }
            else
            {
                result.push_back(static_cast<char>(std::tolower(ch)));
            }
        }

        return result;
    }
}
