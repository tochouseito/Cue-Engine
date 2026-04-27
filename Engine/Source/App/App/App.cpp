// === Base includes ===
#include <CueAssert.h>
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>
#include <IO/Path.h>

// === Windows includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Audio includes ===
#include <AudioBackendFactory.h>

// === Engine includes ===
#include <Engine.h>
#include <GameCore/SceneSerializer.h>
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

#if defined(CUE_STATIC_GAME_LINK)
#include <Native/ScriptAbi.h>
#endif

// === C++ includes ===
#include <algorithm>
#include <vector>

using namespace Cue;

namespace
{
    [[nodiscard]] Result resolve_executable_directory(
        Core::IO::Path& a_outExecutableDirectory) noexcept
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD written = ::GetModuleFileNameW(
            nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (written == 0 || written >= std::size(buffer))
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Executable path could not be resolved.");
        }

        std::string executablePath{};
        Result result = PAL::Win::wide_to_utf8(
            std::wstring_view(buffer, written),
            &executablePath);
        if (!result)
        {
            return result;
        }

        a_outExecutableDirectory = Core::IO::Path(executablePath).parent();
        return Result::ok();
    }

    struct ProjectSettings final
    {
        std::string startupScene{};
        std::string assetRoot = "Assets";
    };

    [[nodiscard]] bool find_json_string_value(
        std::string_view a_text,
        std::string_view a_key,
        std::string& a_outValue) noexcept
    {
        a_outValue.clear();

        const std::string quotedKey = std::string("\"") + std::string(a_key) + "\"";
        size_t keyPos = a_text.find(quotedKey);
        if (keyPos == std::string_view::npos)
        {
            return false;
        }

        size_t colonPos = a_text.find(':', keyPos + quotedKey.size());
        if (colonPos == std::string_view::npos)
        {
            return false;
        }

        size_t valuePos = colonPos + 1;
        while (valuePos < a_text.size())
        {
            const char ch = a_text[valuePos];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            {
                break;
            }
            ++valuePos;
        }

        if (valuePos >= a_text.size() || a_text[valuePos] != '"')
        {
            return false;
        }

        ++valuePos;
        while (valuePos < a_text.size())
        {
            const char ch = a_text[valuePos];
            if (ch == '\\')
            {
                ++valuePos;
                if (valuePos >= a_text.size())
                {
                    return false;
                }

                const char escaped = a_text[valuePos];
                switch (escaped)
                {
                case '\\':
                case '"':
                case '/':
                    a_outValue.push_back(escaped);
                    break;

                case 'n':
                    a_outValue.push_back('\n');
                    break;

                case 'r':
                    a_outValue.push_back('\r');
                    break;

                case 't':
                    a_outValue.push_back('\t');
                    break;

                default:
                    return false;
                }

                ++valuePos;
                continue;
            }

            if (ch == '"')
            {
                return true;
            }

            a_outValue.push_back(ch);
            ++valuePos;
        }

        return false;
    }

    [[nodiscard]] Result load_project_settings(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_projectFilePath,
        ProjectSettings& a_outSettings) noexcept
    {
        std::vector<std::byte> fileData{};
        Result result = a_fileSystem.read_all(a_projectFilePath, &fileData);
        if (!result)
        {
            return result;
        }

        const std::string text(
            reinterpret_cast<const char*>(fileData.data()),
            fileData.size());
        if (!find_json_string_value(text, "startupScene", a_outSettings.startupScene))
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "cueproject.json の startupScene を取得できませんでした。");
        }

        if (a_outSettings.startupScene.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Project startup scene is empty.");
        }

        if (!find_json_string_value(text, "assetRoot", a_outSettings.assetRoot))
        {
            a_outSettings.assetRoot = "Assets";
        }

        return Result::ok();
    }

    [[nodiscard]] Result resolve_project_root(
        Core::IO::IFileSystem& a_fileSystem,
        Core::IO::Path& a_outProjectRoot) noexcept
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD written = ::GetModuleFileNameW(
            nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (written == 0 || written >= std::size(buffer))
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Executable path could not be resolved.");
        }

        std::string executablePath{};
        Result result = PAL::Win::wide_to_utf8(
            std::wstring_view(buffer, written),
            &executablePath);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path executableDirectory =
            Core::IO::Path(executablePath).parent();
        const Core::IO::Path packagedProjectFile = Core::IO::Path::join(
            executableDirectory,
            Core::IO::Path("cueproject.json"));

        bool exists = false;
        Result fileResult = a_fileSystem.exists(packagedProjectFile, &exists);
        if (!fileResult)
        {
            return fileResult;
        }
        if (exists)
        {
            a_outProjectRoot = executableDirectory;
            return Result::ok();
        }

#if defined(CUE_PROJECT_ROOT_PATH)
        const Core::IO::Path repositoryRoot(CUE_PROJECT_ROOT_PATH);
        const Core::IO::Path defaultProjectRoot = Core::IO::Path::join(
            repositoryRoot,
            Core::IO::Path("TestProject"));
        const Core::IO::Path defaultProjectFile = Core::IO::Path::join(
            defaultProjectRoot,
            Core::IO::Path("cueproject.json"));

        exists = false;
        fileResult = a_fileSystem.exists(defaultProjectFile, &exists);
        if (!fileResult)
        {
            return fileResult;
        }
        if (exists)
        {
            a_outProjectRoot = defaultProjectRoot;
            return Result::ok();
        }

        a_outProjectRoot = repositoryRoot;
        return Result::ok();
#else
        return Result::fail(Code::NotFound, Severity::Error,
            "Project root could not be resolved.");
#endif
    }

    [[nodiscard]] Result load_startup_scene(
        Engine& a_engine,
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_projectRoot,
        const ProjectSettings& a_settings) noexcept
    {
        Core::IO::Path assetRoot(a_settings.assetRoot);
        if (!assetRoot.is_absolute())
        {
            assetRoot = Core::IO::Path::join(a_projectRoot, assetRoot);
        }
        a_engine.set_asset_root_path(assetRoot);

        Result result = Result::ok();
        const Core::IO::Path textureRoot = Core::IO::Path::join(
            assetRoot, Core::IO::Path("Textures"));
        bool textureRootExists = false;
        result = a_fileSystem.exists(textureRoot, &textureRootExists);
        if (!result)
        {
            return result;
        }
        if (textureRootExists)
        {
            std::vector<Core::IO::Path> texturePaths{};
            result = a_fileSystem.list_directory(textureRoot, &texturePaths);
            if (!result)
            {
                return result;
            }

            std::sort(texturePaths.begin(), texturePaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            for (const Core::IO::Path& texturePath : texturePaths)
            {
                if (texturePath.extension() != ".cuetexture")
                {
                    continue;
                }

                const std::string textureName = Core::IO::Path::join(
                    Core::IO::Path("Textures"),
                    Core::IO::Path(texturePath.filename())).utf8();
                uint32_t textureId = AssetManager::k_errorTextureId;
                result = a_engine.asset_manager().register_texture_from_cuetexture(
                    a_fileSystem,
                    textureName,
                    texturePath,
                    textureId);
                if (!result)
                {
                    return result;
                }
            }
        }

        const Core::IO::Path modelRoot = Core::IO::Path::join(
            assetRoot, Core::IO::Path("Models"));
        bool modelRootExists = false;
        result = a_fileSystem.exists(modelRoot, &modelRootExists);
        if (!result)
        {
            return result;
        }
        if (modelRootExists)
        {
            std::vector<Core::IO::Path> modelPaths{};
            result = a_fileSystem.list_directory(modelRoot, &modelPaths);
            if (!result)
            {
                return result;
            }

            std::sort(modelPaths.begin(), modelPaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            for (const Core::IO::Path& modelPath : modelPaths)
            {
                if (modelPath.extension() != ".cuemodel")
                {
                    continue;
                }

                const std::string modelName = modelPath.stem();
                ModelHandle modelHandle{};
                result = a_engine.asset_manager().register_model_from_cuemodel(
                    a_fileSystem,
                    modelName,
                    modelPath,
                    modelHandle);
                if (!result)
                {
                    return result;
                }
            }
        }

        const Core::IO::Path materialRoot = Core::IO::Path::join(
            assetRoot, Core::IO::Path("Materials"));
        bool materialRootExists = false;
        result = a_fileSystem.exists(materialRoot, &materialRootExists);
        if (!result)
        {
            return result;
        }
        if (materialRootExists)
        {
            std::vector<Core::IO::Path> materialPaths{};
            result = a_fileSystem.list_directory(materialRoot, &materialPaths);
            if (!result)
            {
                return result;
            }

            std::sort(materialPaths.begin(), materialPaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            for (const Core::IO::Path& materialPath : materialPaths)
            {
                if (materialPath.extension() != ".cuematerial")
                {
                    continue;
                }

                MaterialHandle materialHandle{};
                result = a_engine.asset_manager().load_material(
                    a_fileSystem, materialPath, materialHandle);
                if (!result)
                {
                    return result;
                }
            }
        }

        Core::IO::Path scenePath(a_settings.startupScene);
        if (!scenePath.is_absolute())
        {
            scenePath = Core::IO::Path::join(a_projectRoot, scenePath);
        }

        GameCore::SceneAsset sceneAsset{};
        GameCore::SceneSerializer::LoadOptions loadOptions{};
        loadOptions.assetManager = &a_engine.asset_manager();
        result = GameCore::SceneSerializer::load_scene_asset(
            a_fileSystem,
            scenePath,
            sceneAsset,
            loadOptions);
        if (!result)
        {
            return result;
        }

        GameCore::GameWorld::LoadSceneResult loadResult{};
        result = a_engine.game_world()->load_scene(sceneAsset, loadResult);
        if (!result)
        {
            return result;
        }

        a_engine.set_editor_scene_id(loadResult.sceneId);
        return a_engine.start_play_mode();
    }

    void log_failure(std::string_view a_step, const Result& a_result)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "%s failed: %s (code: %s, severity: %s) at %s:%u in function %s",
            a_step.data(), a_result.message.data(), Cue::to_string(a_result.code),
            Cue::to_string(a_result.severity), a_result.file, a_result.line,
            a_result.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "{} failed: {} (code: {}, severity: {}) at {}:{} in function {}",
            a_step, a_result.message, Cue::to_string(a_result.code),
            Cue::to_string(a_result.severity), a_result.file, a_result.line,
            a_result.function);
#endif
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    constexpr uint32_t k_width = 1280;
    constexpr uint32_t k_height = 720;
    constexpr uint32_t k_bufferCount = 3;
    constexpr uint32_t k_maxFps = 60;
    constexpr bool k_enableDebugLayer = true;

    Result result = Result::ok();
    Core::CQRS::Bridge platformBridge{};

    auto platform = std::make_unique<PAL::Win::WinPlatform>();
    platform->set_platform_bridge(&platformBridge);

    PAL::PlatformSetupInfo platformInfo{};
    platformInfo.width = k_width;
    platformInfo.height = k_height;
    platformInfo.className = "CueAppWindowClass";
    platformInfo.title = "Cue App";

    result = platform->initialize(platformInfo);
    if (!result)
    {
        log_failure("Platform initialize", result);
        return -1;
    }

    auto backend = std::make_unique<RHI::DX12::D3D12Backend>();
    backend->set_win_platform(platform.get());

    auto audioBackend = Audio::create_backend();
    if (audioBackend == nullptr)
    {
        return -1;
    }

    RHI::BackendSetupInfo backendInfo{};
    backendInfo.enableDebugLayer = k_enableDebugLayer;
    backendInfo.width = k_width;
    backendInfo.height = k_height;
    backendInfo.bufferCount = k_bufferCount;

    result = backend->initialize(backendInfo);
    if (!result)
    {
        log_failure("Backend initialize", result);
        return -1;
    }

    result = audioBackend->initialize();
    if (!result)
    {
        log_failure("Audio backend initialize", result);
        backend->shutdown();
        return -1;
    }

    auto engine = std::make_unique<Engine>();

    EngineSetupInfo engineInfo{};
    engineInfo.platform = platform.get();
    engineInfo.backend = backend.get();
    engineInfo.audioBackend = audioBackend.get();
    engineInfo.maxFps = k_maxFps;
    engineInfo.platformBridge = &platformBridge;
    Core::IO::Path executableDirectory{};
    result = resolve_executable_directory(executableDirectory);
    if (!result)
    {
        log_failure("Resolve executable directory", result);
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }
    engineInfo.errorTexturePath = Core::IO::Path::join(
        executableDirectory,
        Core::IO::Path("EngineResources/Textures/CueDummy.cuetexture"));

    result = engine->initialize(engineInfo);
    if (!result)
    {
        log_failure("Engine initialize", result);
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

#if defined(CUE_STATIC_GAME_LINK)
    result = engine->load_static_script_module(
        &cue_script_get_abi_version,
        &cue_script_get_exports);
    if (!result)
    {
        log_failure("Static game module load", result);
        engine->shutdown();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }
#endif

    Core::IO::Path projectRoot{};
    result = resolve_project_root(platform->file_system(), projectRoot);
    if (!result)
    {
        log_failure("Resolve project root", result);
        engine->shutdown();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    ProjectSettings projectSettings{};
    result = load_project_settings(
        platform->file_system(),
        Core::IO::Path::join(projectRoot, Core::IO::Path("cueproject.json")),
        projectSettings);
    if (!result)
    {
        log_failure("Project settings load", result);
        engine->shutdown();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    result = load_startup_scene(
        *engine, platform->file_system(), projectRoot, projectSettings);
    if (!result)
    {
        log_failure("Startup scene load", result);
        engine->shutdown();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    result = platform->start();
    if (!result)
    {
        log_failure("Platform start", result);
        engine->shutdown();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    bool isRunning = true;
    while (isRunning)
    {
        const PAL::PlatformMessage message = platform->poll_message();
        if (message == PAL::PlatformMessage::Quit)
        {
            isRunning = false;
            break;
        }

        result = engine->begin_frame();
        if (!result)
        {
            log_failure("Engine begin_frame", result);
            break;
        }

        result = engine->tick();
        if (!result)
        {
            log_failure("Engine tick", result);
            break;
        }

        result = engine->end_frame();
        if (!result)
        {
            log_failure("Engine end_frame", result);
            break;
        }
    }

    engine->shutdown();
    engine.reset();

    audioBackend->shutdown();
    audioBackend.reset();

    backend->shutdown();
    backend.reset();

    return 0;
}
