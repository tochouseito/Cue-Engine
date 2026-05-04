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
            meshFilter.modelName = "Cube";
            meshFilter.meshId = 0;
            cube.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.materialHandle = {};
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
        const std::string engineRootPath =
            Core::IO::Path(CUE_PROJECT_ROOT_PATH).normalize().utf8();
        const std::string rootCMakeText =
            "cmake_minimum_required(VERSION 4.2.0)\n"
            "\n"
            "project(" + cmakeProjectName + "Scripts VERSION 0.0.1 LANGUAGES C CXX)\n"
            "\n"
            "set(CMAKE_CXX_STANDARD 20)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_CXX_EXTENSIONS OFF)\n"
            "\n"
            "set(_cue_engine_search_root \"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
            "set(CUE_ENGINE_ROOT \"\")\n"
            "\n"
            "while(TRUE)\n"
            "    if(EXISTS \"${_cue_engine_search_root}/Engine/Source/Runtime/Core/Native/ScriptAbi.h\")\n"
            "        set(CUE_ENGINE_ROOT \"${_cue_engine_search_root}\")\n"
            "        break()\n"
            "    endif()\n"
            "\n"
            "    get_filename_component(_cue_engine_parent \"${_cue_engine_search_root}\" DIRECTORY)\n"
            "    if(_cue_engine_parent STREQUAL _cue_engine_search_root)\n"
            "        break()\n"
            "    endif()\n"
            "\n"
            "    set(_cue_engine_search_root \"${_cue_engine_parent}\")\n"
            "endwhile()\n"
            "\n"
            "if(CUE_ENGINE_ROOT STREQUAL \"\" AND DEFINED ENV{CUE_ENGINE_ROOT})\n"
            "    set(CUE_ENGINE_ROOT \"$ENV{CUE_ENGINE_ROOT}\")\n"
            "endif()\n"
            "\n"
            "if(CUE_ENGINE_ROOT STREQUAL \"\")\n"
            "    set(CUE_ENGINE_ROOT \"" + engineRootPath + "\")\n"
            "endif()\n"
            "\n"
            "if(NOT EXISTS \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h\")\n"
            "    message(FATAL_ERROR\n"
            "        \"Cue Engine root not found. Place the project under the CueEngine repository or set CUE_ENGINE_ROOT.\")\n"
            "endif()\n"
            "\n"
            "unset(_cue_engine_search_root)\n"
            "unset(_cue_engine_parent)\n"
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
            "set(CUE_ENGINE_OUTPUT_DIR \"${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>\")\n"
            "set(CUE_ENGINE_VCPKG_TARGET_TRIPLET \"x64-windows-static-md\")\n"
            "set(CUE_ENGINE_VCPKG_TARGET_ROOT\n"
            "    \"${CUE_ENGINE_ROOT}/out/build/win-x64/vcpkg_installed/${CUE_ENGINE_VCPKG_TARGET_TRIPLET}\")\n"
            "\n"
            "set(CUE_GAME_RELEASE_EXECUTABLE_NAME \"Game\")\n"
            "set(CUE_GAME_RELEASE_WINDOW_TITLE \"Cue App\")\n"
            "set(CUE_GAME_RELEASE_ICON_PATH \"\")\n"
            "\n"
            "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/cueproject.json\")\n"
            "    file(READ \"${CMAKE_CURRENT_SOURCE_DIR}/cueproject.json\" CUE_PROJECT_JSON)\n"
            "\n"
            "    string(JSON CUE_PROJECT_NAME ERROR_VARIABLE CUE_PROJECT_JSON_ERROR\n"
            "        GET \"${CUE_PROJECT_JSON}\" name)\n"
            "    if(CUE_PROJECT_JSON_ERROR STREQUAL \"NOTFOUND\" AND NOT CUE_PROJECT_NAME STREQUAL \"\")\n"
            "        set(CUE_GAME_RELEASE_WINDOW_TITLE \"${CUE_PROJECT_NAME}\")\n"
            "    endif()\n"
            "\n"
            "    string(JSON CUE_PROJECT_EXE_NAME ERROR_VARIABLE CUE_PROJECT_JSON_ERROR\n"
            "        GET \"${CUE_PROJECT_JSON}\" gameReleaseExecutableName)\n"
            "    if(CUE_PROJECT_JSON_ERROR STREQUAL \"NOTFOUND\" AND NOT CUE_PROJECT_EXE_NAME STREQUAL \"\")\n"
            "        set(CUE_GAME_RELEASE_EXECUTABLE_NAME \"${CUE_PROJECT_EXE_NAME}\")\n"
            "    endif()\n"
            "\n"
            "    string(JSON CUE_PROJECT_WINDOW_TITLE ERROR_VARIABLE CUE_PROJECT_JSON_ERROR\n"
            "        GET \"${CUE_PROJECT_JSON}\" gameReleaseWindowTitle)\n"
            "    if(CUE_PROJECT_JSON_ERROR STREQUAL \"NOTFOUND\" AND NOT CUE_PROJECT_WINDOW_TITLE STREQUAL \"\")\n"
            "        set(CUE_GAME_RELEASE_WINDOW_TITLE \"${CUE_PROJECT_WINDOW_TITLE}\")\n"
            "    endif()\n"
            "\n"
            "    string(JSON CUE_PROJECT_ICON_PATH ERROR_VARIABLE CUE_PROJECT_JSON_ERROR\n"
            "        GET \"${CUE_PROJECT_JSON}\" gameReleaseIconPath)\n"
            "    if(CUE_PROJECT_JSON_ERROR STREQUAL \"NOTFOUND\" AND NOT CUE_PROJECT_ICON_PATH STREQUAL \"\")\n"
            "        set(CUE_GAME_RELEASE_ICON_PATH \"${CUE_PROJECT_ICON_PATH}\")\n"
            "    endif()\n"
            "endif()\n"
            "\n"
            "if(CUE_GAME_RELEASE_EXECUTABLE_NAME MATCHES \"\\\\.[eE][xX][eE]$\")\n"
            "    get_filename_component(CUE_GAME_RELEASE_EXECUTABLE_NAME\n"
            "        \"${CUE_GAME_RELEASE_EXECUTABLE_NAME}\" NAME_WE)\n"
            "endif()\n"
            "\n"
            "if(CUE_GAME_RELEASE_WINDOW_TITLE STREQUAL \"\")\n"
            "    set(CUE_GAME_RELEASE_WINDOW_TITLE \"Cue App\")\n"
            "endif()\n"
            "\n"
            "set(CUE_GAME_RELEASE_RESOURCE_SOURCES \"\")\n"
            "if(NOT CUE_GAME_RELEASE_ICON_PATH STREQUAL \"\")\n"
            "    if(NOT IS_ABSOLUTE \"${CUE_GAME_RELEASE_ICON_PATH}\")\n"
            "        set(CUE_GAME_RELEASE_ICON_PATH\n"
            "            \"${CMAKE_CURRENT_SOURCE_DIR}/${CUE_GAME_RELEASE_ICON_PATH}\")\n"
            "    endif()\n"
            "\n"
            "    if(EXISTS \"${CUE_GAME_RELEASE_ICON_PATH}\")\n"
            "        get_filename_component(\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION\n"
            "            \"${CUE_GAME_RELEASE_ICON_PATH}\"\n"
            "            EXT)\n"
            "        string(TOLOWER\n"
            "            \"${CUE_GAME_RELEASE_ICON_EXTENSION}\"\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION)\n"
            "\n"
            "        if(CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL \".ico\")\n"
            "            set(CUE_GAME_RELEASE_ICON_RESOURCE_PATH\n"
            "                \"${CUE_GAME_RELEASE_ICON_PATH}\")\n"
            "        elseif(\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL \".png\" OR\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL \".jpg\" OR\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL \".jpeg\" OR\n"
            "            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL \".bmp\")\n"
            "            find_program(CUE_PWSH_EXECUTABLE NAMES pwsh powershell)\n"
            "            if(CUE_PWSH_EXECUTABLE STREQUAL \"CUE_PWSH_EXECUTABLE-NOTFOUND\")\n"
            "                message(FATAL_ERROR\n"
            "                    \"PowerShell was not found. It is required to generate an icon from an image.\")\n"
            "            endif()\n"
            "\n"
            "            set(CUE_GAME_RELEASE_ICON_RESOURCE_PATH\n"
            "                \"${CMAKE_CURRENT_BINARY_DIR}/CueAppIcon.ico\")\n"
            "            execute_process(\n"
            "                COMMAND\n"
            "                    \"${CUE_PWSH_EXECUTABLE}\"\n"
            "                    -NoProfile\n"
            "                    -ExecutionPolicy\n"
            "                    Bypass\n"
            "                    -File\n"
            "                    \"${CUE_ENGINE_ROOT}/Tools/PowerShell/GenerateIco.ps1\"\n"
            "                    -InputPath\n"
            "                    \"${CUE_GAME_RELEASE_ICON_PATH}\"\n"
            "                    -OutputPath\n"
            "                    \"${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}\"\n"
            "                RESULT_VARIABLE CUE_GAME_RELEASE_ICON_RESULT\n"
            "            )\n"
            "            if(NOT CUE_GAME_RELEASE_ICON_RESULT EQUAL 0)\n"
            "                message(FATAL_ERROR\n"
            "                    \"Failed to generate an icon from ${CUE_GAME_RELEASE_ICON_PATH}.\")\n"
            "            endif()\n"
            "        else()\n"
            "            message(FATAL_ERROR\n"
            "                \"gameReleaseIconPath supports .ico, .png, .jpg, .jpeg and .bmp.\")\n"
            "        endif()\n"
            "\n"
            "        file(TO_CMAKE_PATH\n"
            "            \"${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}\"\n"
            "            CUE_GAME_RELEASE_ICON_RESOURCE_PATH)\n"
            "        set(CUE_GAME_RELEASE_RC_PATH\n"
            "            \"${CMAKE_CURRENT_BINARY_DIR}/CueAppResources.rc\")\n"
            "        file(WRITE \"${CUE_GAME_RELEASE_RC_PATH}\"\n"
            "            \"#define IDI_CUE_APP 101\\n\\nIDI_CUE_APP ICON \\\"${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}\\\"\\n\")\n"
            "        list(APPEND CUE_GAME_RELEASE_RESOURCE_SOURCES\n"
            "            \"${CUE_GAME_RELEASE_RC_PATH}\")\n"
            "    endif()\n"
            "endif()\n"
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
            "add_library(GameSources OBJECT\n"
            "    \"EngineModule/ScriptRegistry.h\"\n"
            "    ${GAME_SCRIPT_SOURCES}\n"
            "    \"${CUE_SCRIPT_REGISTRY_CPP}\"\n"
            ")\n"
            "\n"
            "target_include_directories(GameSources PRIVATE\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/EngineModule\"\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/Assets\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine\"\n"
            "    \"${CUE_SCRIPT_GENERATED_DIR}\"\n"
            ")\n"
            "\n"
            "if(MSVC)\n"
            "    target_compile_options(GameSources PRIVATE\n"
            "        /utf-8\n"
            "        /W4\n"
            "        /EHsc\n"
            "        /wd4201\n"
            "    )\n"
            "endif()\n"
            "\n"
            "add_library(GameScript SHARED\n"
            "    $<TARGET_OBJECTS:GameSources>\n"
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
            ")\n"
            "\n"
            "add_library(Game STATIC\n"
            "    $<TARGET_OBJECTS:GameSources>\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/ScriptFramework/GameScriptModule.cpp\"\n"
            ")\n"
            "\n"
            "target_include_directories(Game PRIVATE\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/EngineModule\"\n"
            "    \"${CMAKE_CURRENT_SOURCE_DIR}/Assets\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine\"\n"
            "    \"${CUE_SCRIPT_GENERATED_DIR}\"\n"
            ")\n"
            "\n"
            "if(MSVC)\n"
            "    target_compile_options(Game PRIVATE\n"
            "        /utf-8\n"
            "        /W4\n"
            "        /EHsc\n"
            "        /wd4201\n"
            "    )\n"
            "endif()\n"
            "\n"
            "set_target_properties(Game PROPERTIES\n"
            "    OUTPUT_NAME \"Game\"\n"
            ")\n"
            "\n"
            "add_executable(CueApp WIN32\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/App/App/App.cpp\"\n"
            "    ${CUE_GAME_RELEASE_RESOURCE_SOURCES}\n"
            ")\n"
            "\n"
            "target_include_directories(CueApp PRIVATE\n"
            "    \"${CUE_ENGINE_ROOT}\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/App/App\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Audio\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Audio/XAudio2\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Base\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/ECS\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL/Win\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Physics\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Physics/Jolt\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI/D3D12\"\n"
            "    \"${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine\"\n"
            ")\n"
            "\n"
            "target_compile_definitions(CueApp PRIVATE\n"
            "    CUE_PROJECT_ROOT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
            "    CUE_STATIC_GAME_LINK=1\n"
            "    CUE_APP_WINDOW_TITLE=\"${CUE_GAME_RELEASE_WINDOW_TITLE}\"\n"
            ")\n"
            "\n"
            "set_target_properties(CueApp PROPERTIES\n"
            "    OUTPUT_NAME \"${CUE_GAME_RELEASE_EXECUTABLE_NAME}\"\n"
            ")\n"
            "\n"
            "if(MSVC)\n"
            "    target_compile_options(CueApp PRIVATE\n"
            "        /utf-8\n"
            "        /W4\n"
            "        /EHsc\n"
            "        /wd4201\n"
            "    )\n"
            "endif()\n"
            "\n"
            "target_link_libraries(CueApp PRIVATE\n"
            "    Game\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/Base.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/Core.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/CueMath.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/ECS.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/PAL.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/Physics.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/RHI.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/Engine.lib\"\n"
            "    \"$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Detour-d.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Detour.lib>\"\n"
            "    \"$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Recast-d.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Recast.lib>\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/win_platform.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/jolt_physics_backend.lib\"\n"
            "    \"$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Jolt.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Jolt.lib>\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/d3d12_backend.lib\"\n"
            "    \"${CUE_ENGINE_OUTPUT_DIR}/xaudio2_backend.lib\"\n"
            "    winmm.lib\n"
            "    ole32.lib\n"
            "    shell32.lib\n"
            "    uuid.lib\n"
            "    d3d12.lib\n"
            "    dinput8.lib\n"
            "    dxgi.lib\n"
            "    dxcompiler.lib\n"
            "    dxguid.lib\n"
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
            { "gameReleaseBuildConfiguration", "Release" },
            { "gameReleaseBuildBackend", "CMake" },
            { "gameReleaseOutputRoot", "Builds/Windows" },
            { "gameReleaseExecutableName", "Game" },
            { "gameReleaseWindowTitle", a_projectName },
            { "gameReleaseIconPath", "" },
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
