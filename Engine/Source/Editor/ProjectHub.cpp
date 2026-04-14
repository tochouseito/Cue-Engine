#include "ProjectHub.h"

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/SceneSerializer.h>

// === PAL includes ===
#include <Dialog/DirectoryDialog.h>

// === C++ includes ===
#include <algorithm>
#include <cctype>
#include <span>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    namespace
    {
        constexpr const char* k_createProjectPopupName = "新規プロジェクト作成";
        constexpr const char* k_defaultProjectRoot = CUE_PROJECT_ROOT_PATH;

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

    ProjectHub::ProjectHub(Core::IO::IFileSystem& a_fileSystem)
        : m_fileSystem(a_fileSystem)
    {
        const std::string defaultDirectory = k_defaultProjectRoot;
        defaultDirectory.copy(
            m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size() - 1
        );
        m_projectDirectoryBuffer.back() = '\0';
    }

    void ProjectHub::update()
    {
        ImGui::Begin("Project Hub");

        if (ImGui::Button("新規プロジェクト作成"))
        {
            open_create_project_modal();
        }

        if (ImGui::Button("プロジェクトを開く"))
        {
            open_existing_project();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        draw_create_project_modal();

        ImGui::End();
    }

    bool ProjectHub::is_open() const
    {
        return m_isOpen;
    }

    std::string ProjectHub::project_path() const
    {
        return m_projectPath;
    }

    void ProjectHub::open_create_project_modal()
    {
        m_errorMessage.clear();
        m_projectNameBuffer.fill('\0');
        m_openCreateProjectModal = true;
        ImGui::OpenPopup(k_createProjectPopupName);
    }

    void ProjectHub::draw_create_project_modal()
    {
        if (m_openCreateProjectModal)
        {
            ImGui::OpenPopup(k_createProjectPopupName);
            m_openCreateProjectModal = false;
        }

        if (!ImGui::BeginPopupModal(
            k_createProjectPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("プロジェクト名");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##ProjectName", m_projectNameBuffer.data(),
            m_projectNameBuffer.size());

        ImGui::Spacing();
        ImGui::Text("作成先ディレクトリ");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##ProjectDirectory", m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("参照..."))
        {
            browse_project_directory();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        const bool canCreate =
            !trim_text(m_projectNameBuffer.data()).empty() &&
            !trim_text(m_projectDirectoryBuffer.data()).empty();

        if (!canCreate)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("作成") && canCreate)
        {
            if (create_project())
            {
                ImGui::CloseCurrentPopup();
            }
        }

        if (!canCreate)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            m_errorMessage.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    bool ProjectHub::browse_project_directory()
    {
        bool wasSelected = false;
        std::string selectedDirectory{};
        const Result result = Cue::PAL::Win::pick_directory_dialog(
            "プロジェクト作成先を選択",
            trim_text(m_projectDirectoryBuffer.data()),
            &selectedDirectory,
            &wasSelected
        );
        if (!result)
        {
            m_errorMessage = "フォルダ選択ダイアログを開けませんでした。";
            return false;
        }

        if (!wasSelected)
        {
            return false;
        }

        selectedDirectory.copy(
            m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size() - 1
        );
        m_projectDirectoryBuffer[
            (std::min)(selectedDirectory.size(), m_projectDirectoryBuffer.size() - 1)] = '\0';
        m_errorMessage.clear();
        return true;
    }

    bool ProjectHub::open_existing_project()
    {
        bool wasSelected = false;
        std::string selectedDirectory{};
        const Result result = Cue::PAL::Win::pick_directory_dialog(
            "プロジェクトフォルダを選択",
            trim_text(m_projectDirectoryBuffer.data()),
            &selectedDirectory,
            &wasSelected
        );
        if (!result)
        {
            m_errorMessage = "フォルダ選択ダイアログを開けませんでした。";
            return false;
        }

        if (!wasSelected)
        {
            return false;
        }

        if (!validate_project_directory(selectedDirectory))
        {
            return false;
        }

        m_projectPath = selectedDirectory;
        m_errorMessage.clear();
        m_isOpen = false;
        return true;
    }

    bool ProjectHub::validate_project_directory(const std::string& a_projectPath)
    {
        const Core::IO::Path projectPath(a_projectPath);

        bool exists = false;
        Result result = m_fileSystem.exists(projectPath, &exists);
        if (!result)
        {
            m_errorMessage = "プロジェクトフォルダの確認に失敗しました。";
            return false;
        }

        if (!exists)
        {
            m_errorMessage = "指定したプロジェクトフォルダが存在しません。";
            return false;
        }

        Core::IO::FileStat directoryStat{};
        result = m_fileSystem.stat(projectPath, &directoryStat);
        if (!result)
        {
            m_errorMessage = "プロジェクトフォルダの情報取得に失敗しました。";
            return false;
        }

        if (directoryStat.type != Core::IO::FileType::directory)
        {
            m_errorMessage = "指定したパスはフォルダではありません。";
            return false;
        }

        const Core::IO::Path projectFilePath = Core::IO::Path::join(
            projectPath,
            Core::IO::Path("cueproject.json"));

        bool projectFileExists = false;
        result = m_fileSystem.exists(projectFilePath, &projectFileExists);
        if (!result)
        {
            m_errorMessage = "cueproject.json の確認に失敗しました。";
            return false;
        }

        if (!projectFileExists)
        {
            m_errorMessage = "cueproject.json が見つかりません。";
            return false;
        }

        Core::IO::FileStat projectFileStat{};
        result = m_fileSystem.stat(projectFilePath, &projectFileStat);
        if (!result)
        {
            m_errorMessage = "cueproject.json の情報取得に失敗しました。";
            return false;
        }

        if (projectFileStat.type != Core::IO::FileType::regular)
        {
            m_errorMessage = "cueproject.json がファイルではありません。";
            return false;
        }

        return true;
    }

    bool ProjectHub::create_project()
    {
        const std::string projectName = trim_text(m_projectNameBuffer.data());
        const std::string baseDirectory = trim_text(m_projectDirectoryBuffer.data());

        if (projectName.empty())
        {
            m_errorMessage = "プロジェクト名を入力してください。";
            return false;
        }

        if (baseDirectory.empty())
        {
            m_errorMessage = "作成先ディレクトリを指定してください。";
            return false;
        }

        if (has_invalid_project_name_character(projectName))
        {
            m_errorMessage =
                "プロジェクト名に使用できない文字が含まれています。";
            return false;
        }

        const Core::IO::Path basePath(baseDirectory);
        bool baseExists = false;
        Result result = m_fileSystem.exists(basePath, &baseExists);
        if (!result)
        {
            m_errorMessage =
                "作成先ディレクトリの確認に失敗しました。";
            return false;
        }

        if (!baseExists)
        {
            m_errorMessage =
                "作成先ディレクトリが存在しないか、ディレクトリではありません。";
            return false;
        }

        Core::IO::FileStat baseStat{};
        result = m_fileSystem.stat(basePath, &baseStat);
        if (!result)
        {
            m_errorMessage =
                "作成先ディレクトリの情報取得に失敗しました。";
            return false;
        }

        if (baseStat.type != Core::IO::FileType::directory)
        {
            m_errorMessage =
                "作成先ディレクトリが存在しないか、ディレクトリではありません。";
            return false;
        }

        const Core::IO::Path projectPath =
            Core::IO::Path::join(basePath, Core::IO::Path(projectName));
        bool projectExists = false;
        result = m_fileSystem.exists(projectPath, &projectExists);
        if (!result)
        {
            m_errorMessage =
                "作成先プロジェクトパスの確認に失敗しました。";
            return false;
        }

        if (projectExists)
        {
            m_errorMessage = "同名のフォルダがすでに存在します。";
            return false;
        }

        result = m_fileSystem.create_directories(projectPath);
        if (!result)
        {
            m_errorMessage =
                "プロジェクトフォルダの作成に失敗しました。";
            return false;
        }

        if (!create_project_directories(projectPath.utf8()))
        {
            return false;
        }

        if (!write_script_project_files(projectName, projectPath.utf8()))
        {
            return false;
        }

        if (!write_default_scene(projectPath.utf8()))
        {
            return false;
        }

        if (!write_project_file(projectName, projectPath.utf8()))
        {
            return false;
        }

        m_projectPath = projectPath.utf8();
        m_errorMessage.clear();
        m_isOpen = false;
        return true;
    }

    bool ProjectHub::create_project_directories(const std::string& a_projectPath)
    {
        const Core::IO::Path rootPath(a_projectPath);
        const std::array<Core::IO::Path, 6> directories = {
            Core::IO::Path::join(rootPath, Core::IO::Path("Assets")),
            Core::IO::Path::join(rootPath, Core::IO::Path("Assets/Scenes")),
            Core::IO::Path::join(rootPath, Core::IO::Path("GameScript")),
            Core::IO::Path::join(rootPath, Core::IO::Path("GameScript/Source")),
            Core::IO::Path::join(rootPath, Core::IO::Path("Saved")),
            Core::IO::Path::join(rootPath, Core::IO::Path("Intermediate")),
        };

        for (const Core::IO::Path& directoryPath : directories)
        {
            const Result result = m_fileSystem.create_directories(directoryPath);
            if (!result)
            {
                m_errorMessage =
                    "プロジェクト初期フォルダの作成に失敗しました。";
                return false;
            }
        }

        return true;
    }

    bool ProjectHub::write_script_project_files(
        const std::string& a_projectName,
        const std::string& a_projectPath)
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
            "    [[nodiscard]] CueResult validate_engine_api(const CueEngineApi* a_engineApi)\n"
            "    {\n"
            "        if (a_engineApi == nullptr)\n"
            "        {\n"
            "            return CueResult_InvalidArgument;\n"
            "        }\n"
            "        if (a_engineApi->structSize < sizeof(CueEngineApi))\n"
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

        const Core::IO::Path rootPath(a_projectPath);
        if (!write_text_file(
            Core::IO::Path::join(rootPath, Core::IO::Path("CMakeLists.txt")),
            rootCMakeText))
        {
            m_errorMessage = "スクリプト用 CMakeLists.txt の作成に失敗しました。";
            return false;
        }

        if (!write_text_file(
            Core::IO::Path::join(rootPath, Core::IO::Path("CMakePresets.json")),
            presetsText))
        {
            m_errorMessage = "スクリプト用 CMakePresets.json の作成に失敗しました。";
            return false;
        }

        if (!write_text_file(
            Core::IO::Path::join(
                rootPath,
                Core::IO::Path("GameScript/CMakeLists.txt")),
            gameScriptCMakeText))
        {
            m_errorMessage =
                "GameScript/CMakeLists.txt の作成に失敗しました。";
            return false;
        }

        if (!write_text_file(
            Core::IO::Path::join(
                rootPath,
                Core::IO::Path("GameScript/Source/GameScriptModule.cpp")),
            gameScriptModuleText))
        {
            m_errorMessage =
                "GameScriptModule.cpp の作成に失敗しました。";
            return false;
        }

        return true;
    }

    bool ProjectHub::write_default_scene(const std::string& a_projectPath)
    {
        GameCore::SceneAsset sceneAsset("Main");
        sceneAsset.add_object(make_default_camera_object());
        sceneAsset.add_object(make_default_cube_object());

        const Core::IO::Path scenePath = Core::IO::Path::join(
            Core::IO::Path(a_projectPath),
            Core::IO::Path("Assets/Scenes/Main.cuescene"));
        const Result result = GameCore::SceneSerializer::save_scene_asset(
            sceneAsset, m_fileSystem, scenePath);
        if (!result)
        {
            m_errorMessage = "Main.cuescene の作成に失敗しました。";
            return false;
        }

        return true;
    }

    bool ProjectHub::write_project_file(
        const std::string& a_projectName,
        const std::string& a_projectPath
    )
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
            Core::IO::Path(a_projectPath),
            Core::IO::Path("cueproject.json"));

        const std::span<const char> textSpan(jsonText.data(), jsonText.size());
        const std::span<const std::byte> byteSpan = std::as_bytes(textSpan);
        const Result result =
            m_fileSystem.write_all(projectFilePath, byteSpan, false);
        if (!result)
        {
            m_errorMessage = "cueproject.json の書き込みに失敗しました。";
            return false;
        }

        return true;
    }

    bool ProjectHub::write_text_file(
        const Core::IO::Path& a_filePath,
        const std::string& a_text)
    {
        const std::span<const char> textSpan(a_text.data(), a_text.size());
        const std::span<const std::byte> byteSpan = std::as_bytes(textSpan);
        const Result result = m_fileSystem.write_all(a_filePath, byteSpan, false);
        return static_cast<bool>(result);
    }

    bool ProjectHub::has_invalid_project_name_character(
        const std::string& a_projectName) const
    {
        static constexpr const char* k_invalidChars = "\\/:*?\"<>|";
        return a_projectName.find_first_of(k_invalidChars) != std::string::npos;
    }

    std::string ProjectHub::make_cmake_project_name(
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

    std::string ProjectHub::trim_text(const char* a_text) const
    {
        if (a_text == nullptr)
        {
            return "";
        }

        std::string text = a_text;
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "";
        }

        const size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, (last - first) + 1);
    }
}
