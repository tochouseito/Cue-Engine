#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>
#include <IO/Path.h>
#include <Time/Timer.h>

// === Engine includes ===
#include <ModelImporter.h>
#include <ModelCooker.h>
#include <SoundCooker.h>
#include <TextureCooker.h>
#include <GameCore/SceneSerializer.h>
#include <Script/MarionnetteObject.h>

// === Win includes ===
#include <shellapi.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::Editor
{
    namespace
    {
        struct ProjectSettings final
        {
            std::string startupScene{};
            std::string assetRoot = "Assets";
            std::string scriptRoot{};
            BuildConfiguration scriptBuildConfiguration =
                BuildConfiguration::Debug;
            BuildConfiguration scriptLoadConfiguration =
                BuildConfiguration::Debug;
            BuildBackend scriptBuildBackend = BuildBackend::CMake;
            BuildConfiguration gameReleaseBuildConfiguration =
                BuildConfiguration::Release;
            BuildBackend gameReleaseBuildBackend = BuildBackend::CMake;
            std::string gameReleaseOutputRoot = "Builds/Windows";
        };

        struct SceneCameraMenuEntry final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            std::string name{};
            bool isMain = false;
        };

        void log_result(std::string_view a_prefix, const Result& a_result)
        {
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "{}: {} (code: {}, severity: {}) at {}:{} in function {}",
                a_prefix, a_result.message, Cue::to_string(a_result.code),
                Cue::to_string(a_result.severity), a_result.file, a_result.line,
                a_result.function);
        }

        void log_build_output(std::string_view a_prefix, std::string_view a_output)
        {
            if (a_output.empty())
            {
                return;
            }

            Core::IO::log(Core::IO::LogSink::debugConsole,
                "{}:\n{}", a_prefix, a_output);
        }

        [[nodiscard]] const char* to_stage_prefix(BuildStage a_stage) noexcept
        {
            switch (a_stage)
            {
            case BuildStage::Configure:
                return "[Script][Configure]";
            case BuildStage::Build:
                return "[Script][Build]";
            case BuildStage::Reload:
                return "[Script][Reload]";
            case BuildStage::Attach:
                return "[Script][Attach]";
            case BuildStage::General:
            default:
                return "[Script]";
            }
        }

        [[nodiscard]] const char* to_stage_name(BuildStage a_stage) noexcept
        {
            switch (a_stage)
            {
            case BuildStage::Configure:
                return "Configure";
            case BuildStage::Build:
                return "Build";
            case BuildStage::Reload:
                return "Reload";
            case BuildStage::Attach:
                return "Attach";
            case BuildStage::General:
            default:
                return "General";
            }
        }

        [[nodiscard]] const char* to_severity_name(
            BuildMessageSeverity a_severity) noexcept
        {
            switch (a_severity)
            {
            case BuildMessageSeverity::Warning:
                return "Warning";
            case BuildMessageSeverity::Error:
                return "Error";
            case BuildMessageSeverity::Info:
            default:
                return "Info";
            }
        }

        [[nodiscard]] bool should_serialize_script_field(
            std::string_view a_scriptClassName,
            std::string_view a_fieldName,
            void* a_userData)
        {
            const Engine* engine = static_cast<const Engine*>(a_userData);
            if (engine == nullptr)
            {
                return true;
            }

            const MarionnetteClass* marionnetteClass =
                engine->find_marionnette_class(a_scriptClassName);
            if (marionnetteClass == nullptr)
            {
                // 未解決 class は保存データを落とさない。
                return true;
            }

            const MarionnetteProperty* property =
                marionnetteClass->find_property(a_fieldName);
            if (property == nullptr)
            {
                return false;
            }

            return has_any_flags(
                property->flags,
                MarionnettePropertyFlag_Serialize);
        }

        void push_build_message(
            BuildResult& a_result,
            BuildMessageSeverity a_severity,
            BuildStage a_stage,
            std::string a_text)
        {
            a_result.messages.push_back(BuildMessage{
                a_severity,
                a_stage,
                std::move(a_text)
            });
        }

        void push_build_message(
            GameReleaseBuildResult& a_result,
            BuildMessageSeverity a_severity,
            BuildStage a_stage,
            std::string a_text)
        {
            a_result.messages.push_back(BuildMessage{
                a_severity,
                a_stage,
                std::move(a_text)
            });
        }

        void append_game_release_result(
            GameReleaseBuildResult& a_destination,
            const GameReleaseBuildResult& a_source)
        {
            a_destination.stageResults.insert(
                a_destination.stageResults.end(),
                a_source.stageResults.begin(),
                a_source.stageResults.end());
            a_destination.messages.insert(
                a_destination.messages.end(),
                a_source.messages.begin(),
                a_source.messages.end());
            a_destination.artifacts.insert(
                a_destination.artifacts.end(),
                a_source.artifacts.begin(),
                a_source.artifacts.end());

            if (!a_source.configureLogPath.is_empty())
            {
                a_destination.configureLogPath = a_source.configureLogPath;
            }
            if (!a_source.buildLogPath.is_empty())
            {
                a_destination.buildLogPath = a_source.buildLogPath;
            }
            if (!a_source.summary.empty())
            {
                a_destination.summary = a_source.summary;
            }

            a_destination.exitCode = a_source.exitCode;
            a_destination.didConfigure =
                a_destination.didConfigure || a_source.didConfigure;
            a_destination.succeeded =
                a_destination.succeeded && a_source.succeeded;
        }

        [[nodiscard]] Result copy_directory_recursive(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourceDirectory,
            const Core::IO::Path& a_destinationDirectory) noexcept
        {
            bool sourceExists = false;
            Result result = a_fileSystem.exists(a_sourceDirectory, &sourceExists);
            if (!result)
            {
                return result;
            }
            if (!sourceExists)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "コピー元ディレクトリが存在しません。");
            }

            result = a_fileSystem.create_directories(a_destinationDirectory);
            if (!result)
            {
                return result;
            }

            std::vector<Core::IO::Path> entries{};
            result = a_fileSystem.list_directory(a_sourceDirectory, &entries);
            if (!result)
            {
                return result;
            }

            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                result = a_fileSystem.stat(entryPath, &stat);
                if (!result)
                {
                    return result;
                }

                const Core::IO::Path destinationPath = Core::IO::Path::join(
                    a_destinationDirectory,
                    Core::IO::Path(entryPath.filename()));

                if (stat.type == Core::IO::FileType::directory)
                {
                    result = copy_directory_recursive(
                        a_fileSystem,
                        entryPath,
                        destinationPath);
                }
                else if (stat.type == Core::IO::FileType::regular)
                {
                    result = a_fileSystem.copy_file(
                        entryPath,
                        destinationPath,
                        true);
                }
                else
                {
                    continue;
                }

                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] bool should_copy_release_asset_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            const std::string extension = a_filePath.extension();
            return extension == ".cuetexture" ||
                extension == ".cuematerial" ||
                extension == ".cuescene" ||
                extension == ".cuemodel" ||
                extension == ".cuesound";
        }

        [[nodiscard]] Result copy_release_assets_recursive(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourceDirectory,
            const Core::IO::Path& a_destinationDirectory) noexcept
        {
            bool sourceExists = false;
            Result result = a_fileSystem.exists(a_sourceDirectory, &sourceExists);
            if (!result)
            {
                return result;
            }
            if (!sourceExists)
            {
                return Result::ok();
            }

            Core::IO::FileStat sourceStat{};
            result = a_fileSystem.stat(a_sourceDirectory, &sourceStat);
            if (!result)
            {
                return result;
            }
            if (sourceStat.type != Core::IO::FileType::directory)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Release asset source path must be a directory.");
            }

            std::vector<Core::IO::Path> entries{};
            result = a_fileSystem.list_directory(a_sourceDirectory, &entries);
            if (!result)
            {
                return result;
            }

            bool hasCopiedEntry = false;
            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                result = a_fileSystem.stat(entryPath, &stat);
                if (!result)
                {
                    return result;
                }

                const Core::IO::Path destinationPath = Core::IO::Path::join(
                    a_destinationDirectory,
                    Core::IO::Path(entryPath.filename()));

                if (stat.type == Core::IO::FileType::directory)
                {
                    result = copy_release_assets_recursive(
                        a_fileSystem,
                        entryPath,
                        destinationPath);
                    if (!result)
                    {
                        return result;
                    }

                    bool destinationExists = false;
                    result = a_fileSystem.exists(destinationPath, &destinationExists);
                    if (!result)
                    {
                        return result;
                    }
                    hasCopiedEntry = hasCopiedEntry || destinationExists;
                    continue;
                }

                if (stat.type != Core::IO::FileType::regular ||
                    !should_copy_release_asset_file(entryPath))
                {
                    continue;
                }

                if (!hasCopiedEntry)
                {
                    result = a_fileSystem.create_directories(a_destinationDirectory);
                    if (!result)
                    {
                        return result;
                    }
                    hasCopiedEntry = true;
                }

                result = a_fileSystem.copy_file(
                    entryPath,
                    destinationPath,
                    true);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result remove_path_recursive(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_targetPath) noexcept
        {
            bool exists = false;
            Result result = a_fileSystem.exists(a_targetPath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::ok();
            }

            Core::IO::FileStat stat{};
            result = a_fileSystem.stat(a_targetPath, &stat);
            if (!result)
            {
                return result;
            }

            if (stat.type == Core::IO::FileType::directory)
            {
                std::vector<Core::IO::Path> entries{};
                result = a_fileSystem.list_directory(a_targetPath, &entries);
                if (!result)
                {
                    return result;
                }

                for (const Core::IO::Path& entryPath : entries)
                {
                    result = remove_path_recursive(a_fileSystem, entryPath);
                    if (!result)
                    {
                        return result;
                    }
                }
            }

            bool removed = false;
            result = a_fileSystem.remove(a_targetPath, &removed);
            if (!result && result.code != Code::NotFound)
            {
                return result;
            }

            return Result::ok();
        }

        [[nodiscard]] Core::IO::Path resolve_game_release_output_directory(
            const Core::IO::Path& a_projectRoot,
            const ProjectSettings& a_settings) noexcept
        {
            Core::IO::Path outputRoot(a_settings.gameReleaseOutputRoot);
            if (!outputRoot.is_absolute())
            {
                outputRoot = Core::IO::Path::join(a_projectRoot, outputRoot);
            }

            return Core::IO::Path::join(
                outputRoot,
                Core::IO::Path("Release"));
        }

        [[nodiscard]] bool has_stage_result(
            const BuildResult& a_result,
            BuildStage a_stage) noexcept
        {
            for (const BuildStageResult& stageResult : a_result.stageResults)
            {
                if (stageResult.stage == a_stage)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] const BuildMessage* find_build_message(
            const BuildResult& a_result,
            BuildMessageSeverity a_severity) noexcept
        {
            for (const BuildMessage& message : a_result.messages)
            {
                if (message.severity == a_severity)
                {
                    return &message;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const BuildMessage* find_build_message(
            const GameReleaseBuildResult& a_result,
            BuildMessageSeverity a_severity) noexcept
        {
            for (const BuildMessage& message : a_result.messages)
            {
                if (message.severity == a_severity)
                {
                    return &message;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const BuildStageResult* find_failed_stage_result(
            const BuildResult& a_result) noexcept
        {
            for (const BuildStageResult& stageResult : a_result.stageResults)
            {
                if (!stageResult.succeeded)
                {
                    return &stageResult;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const BuildStageResult* find_failed_stage_result(
            const GameReleaseBuildResult& a_result) noexcept
        {
            for (const BuildStageResult& stageResult : a_result.stageResults)
            {
                if (!stageResult.succeeded)
                {
                    return &stageResult;
                }
            }

            return nullptr;
        }

        [[nodiscard]] std::string make_output_excerpt(
            std::string_view a_output) noexcept
        {
            size_t lineBegin = 0;
            while (lineBegin < a_output.size())
            {
                const size_t lineEnd = a_output.find_first_of("\r\n", lineBegin);
                const size_t lineSize =
                    lineEnd == std::string_view::npos
                    ? (a_output.size() - lineBegin)
                    : (lineEnd - lineBegin);
                if (lineSize > 0)
                {
                    std::string excerpt(a_output.substr(lineBegin, lineSize));
                    if (excerpt.size() > 240)
                    {
                        excerpt.resize(240);
                        excerpt += "...";
                    }

                    return excerpt;
                }

                if (lineEnd == std::string_view::npos)
                {
                    break;
                }

                lineBegin = lineEnd + 1;
            }

            return {};
        }

        [[nodiscard]] Result load_project_materials(
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

            const Core::IO::Path materialRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Materials"));
            bool materialRootExists = false;
            Result result = a_fileSystem.exists(materialRoot, &materialRootExists);
            if (!result)
            {
                return result;
            }
            if (!materialRootExists)
            {
                return Result::ok();
            }

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

            return Result::ok();
        }

        [[nodiscard]] Result load_project_models(
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

            const Core::IO::Path modelRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Models"));
            bool modelRootExists = false;
            Result result = a_fileSystem.exists(modelRoot, &modelRootExists);
            if (!result)
            {
                return result;
            }
            if (!modelRootExists)
            {
                return Result::ok();
            }

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

            std::vector<Core::IO::Path> cookedModelPaths{};
            std::vector<Core::IO::Path> sourceModelPaths{};
            for (const Core::IO::Path& modelPath : modelPaths)
            {
                if (modelPath.extension() == ".cuemodel")
                {
                    cookedModelPaths.push_back(modelPath);
                }
                else if (modelPath.extension() == ".obj")
                {
                    sourceModelPaths.push_back(modelPath);
                }
            }

            for (const Core::IO::Path& sourceModelPath : sourceModelPaths)
            {
                const Core::IO::Path cookedModelPath = Core::IO::Path::join(
                    modelRoot,
                    Core::IO::Path(sourceModelPath.stem() + ".cuemodel"));
                result = ModelCooker::ensure_cuemodel_is_up_to_date(
                    a_fileSystem,
                    sourceModelPath,
                    cookedModelPath);
                if (!result)
                {
                    return result;
                }

                bool hasCookedModel = false;
                result = a_fileSystem.exists(cookedModelPath, &hasCookedModel);
                if (!result)
                {
                    return result;
                }
                if (hasCookedModel)
                {
                    cookedModelPaths.push_back(cookedModelPath);
                }
            }

            std::sort(cookedModelPaths.begin(), cookedModelPaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            cookedModelPaths.erase(
                std::unique(
                    cookedModelPaths.begin(),
                    cookedModelPaths.end(),
                    [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                    {
                        return a_left.normalize().utf8() == a_right.normalize().utf8();
                    }),
                cookedModelPaths.end());

            for (const Core::IO::Path& cookedModelPath : cookedModelPaths)
            {
                const std::string modelName = cookedModelPath.stem();
                ModelHandle modelHandle{};
                result = a_engine.asset_manager().register_model_from_cuemodel(
                    a_fileSystem,
                    modelName,
                    cookedModelPath,
                    modelHandle);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result load_project_textures(
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

            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Textures"));
            bool textureRootExists = false;
            Result result = a_fileSystem.exists(textureRoot, &textureRootExists);
            if (!result)
            {
                return result;
            }
            if (!textureRootExists)
            {
                return Result::ok();
            }

            std::vector<Core::IO::Path> texturePaths{};
            result = a_fileSystem.list_directory(textureRoot, &texturePaths);
            if (!result)
            {
                return result;
            }

            std::vector<Core::IO::Path> cookedTexturePaths{};
            std::vector<Core::IO::Path> sourceTexturePaths{};
            for (const Core::IO::Path& texturePath : texturePaths)
            {
                if (texturePath.extension() == ".cuetexture")
                {
                    cookedTexturePaths.push_back(texturePath);
                }
                else if (texturePath.extension() == ".png")
                {
                    sourceTexturePaths.push_back(texturePath);
                }
            }

            std::sort(sourceTexturePaths.begin(), sourceTexturePaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            for (const Core::IO::Path& sourceTexturePath : sourceTexturePaths)
            {
                const Core::IO::Path cookedTexturePath = Core::IO::Path::join(
                    textureRoot,
                    Core::IO::Path(sourceTexturePath.stem() + ".cuetexture"));
                result = TextureCooker::ensure_cuetexture_is_up_to_date(
                    a_fileSystem,
                    sourceTexturePath,
                    cookedTexturePath);
                if (!result)
                {
                    return result;
                }
                cookedTexturePaths.push_back(cookedTexturePath);
            }

            std::sort(cookedTexturePaths.begin(), cookedTexturePaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            cookedTexturePaths.erase(
                std::unique(cookedTexturePaths.begin(), cookedTexturePaths.end(),
                    [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                    {
                        return a_left.normalize().utf8() == a_right.normalize().utf8();
                    }),
                cookedTexturePaths.end());

            for (const Core::IO::Path& cookedTexturePath : cookedTexturePaths)
            {
                const std::string textureName = Core::IO::Path::join(
                    Core::IO::Path("Textures"),
                    Core::IO::Path(cookedTexturePath.filename())).utf8();
                uint32_t textureId = AssetManager::k_errorTextureId;
                result = a_engine.asset_manager().register_texture_from_cuetexture(
                    a_fileSystem,
                    textureName,
                    cookedTexturePath,
                    textureId);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result cook_project_sounds(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectRoot,
            const ProjectSettings& a_settings) noexcept
        {
            Core::IO::Path assetRoot(a_settings.assetRoot);
            if (!assetRoot.is_absolute())
            {
                assetRoot = Core::IO::Path::join(a_projectRoot, assetRoot);
            }

            const Core::IO::Path soundRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Sounds"));
            bool soundRootExists = false;
            Result result = a_fileSystem.exists(soundRoot, &soundRootExists);
            if (!result)
            {
                return result;
            }
            if (!soundRootExists)
            {
                return Result::ok();
            }

            std::vector<Core::IO::Path> soundPaths{};
            result = a_fileSystem.list_directory(soundRoot, &soundPaths);
            if (!result)
            {
                return result;
            }

            std::sort(soundPaths.begin(), soundPaths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });

            for (const Core::IO::Path& sourceSoundPath : soundPaths)
            {
                if (sourceSoundPath.extension() != ".wav")
                {
                    continue;
                }

                const Core::IO::Path cookedSoundPath = Core::IO::Path::join(
                    soundRoot,
                    Core::IO::Path(sourceSoundPath.stem() + ".cuesound"));
                result = SoundCooker::ensure_cuesound_is_up_to_date(
                    a_fileSystem,
                    sourceSoundPath,
                    cookedSoundPath);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] std::string make_primary_build_message(
            const BuildResult& a_result) noexcept
        {
            if (const BuildMessage* errorMessage =
                find_build_message(a_result, BuildMessageSeverity::Error);
                errorMessage != nullptr && !errorMessage->text.empty())
            {
                return errorMessage->text;
            }

            if (const BuildMessage* warningMessage =
                find_build_message(a_result, BuildMessageSeverity::Warning);
                warningMessage != nullptr && !warningMessage->text.empty())
            {
                return warningMessage->text;
            }

            if (const BuildStageResult* failedStageResult =
                find_failed_stage_result(a_result);
                failedStageResult != nullptr)
            {
                const std::string excerpt =
                    make_output_excerpt(failedStageResult->output);
                if (!excerpt.empty())
                {
                    return excerpt;
                }
            }

            if (!a_result.summary.empty())
            {
                return a_result.summary;
            }

            return {};
        }

        [[nodiscard]] std::string make_primary_build_message(
            const GameReleaseBuildResult& a_result) noexcept
        {
            if (const BuildMessage* errorMessage =
                find_build_message(a_result, BuildMessageSeverity::Error);
                errorMessage != nullptr && !errorMessage->text.empty())
            {
                return errorMessage->text;
            }

            if (const BuildMessage* warningMessage =
                find_build_message(a_result, BuildMessageSeverity::Warning);
                warningMessage != nullptr && !warningMessage->text.empty())
            {
                return warningMessage->text;
            }

            if (const BuildStageResult* failedStageResult =
                find_failed_stage_result(a_result);
                failedStageResult != nullptr)
            {
                const std::string excerpt =
                    make_output_excerpt(failedStageResult->output);
                if (!excerpt.empty())
                {
                    return excerpt;
                }
            }

            if (!a_result.summary.empty())
            {
                return a_result.summary;
            }

            return {};
        }

        [[nodiscard]] Result parse_build_configuration(
            std::string_view a_text,
            BuildConfiguration& a_outConfiguration) noexcept
        {
            if (a_text == "Debug")
            {
                a_outConfiguration = BuildConfiguration::Debug;
                return Result::ok();
            }

            if (a_text == "RelWithDebInfo")
            {
                a_outConfiguration = BuildConfiguration::RelWithDebInfo;
                return Result::ok();
            }

            if (a_text == "Release")
            {
                a_outConfiguration = BuildConfiguration::Release;
                return Result::ok();
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "scriptBuildConfiguration が不正です。");
        }

        [[nodiscard]] Result parse_build_backend(
            std::string_view a_text,
            BuildBackend& a_outBackend) noexcept
        {
            if (a_text == "CMake")
            {
                a_outBackend = BuildBackend::CMake;
                return Result::ok();
            }

            if (a_text == "VisualStudio")
            {
                a_outBackend = BuildBackend::VisualStudio;
                return Result::ok();
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "scriptBuildBackend が不正です。");
        }

        [[nodiscard]] const char* to_build_backend_name(
            BuildBackend a_backend) noexcept
        {
            switch (a_backend)
            {
            case BuildBackend::CMake:
                return "CMake";
            case BuildBackend::VisualStudio:
                return "VisualStudio";
            }

            return "CMake";
        }

        [[nodiscard]] ScriptModuleBuildConfiguration
            to_script_module_build_configuration(
                BuildConfiguration a_configuration) noexcept
        {
            switch (a_configuration)
            {
            case BuildConfiguration::Debug:
                return ScriptModuleBuildConfiguration::Debug;

            case BuildConfiguration::RelWithDebInfo:
                return ScriptModuleBuildConfiguration::RelWithDebInfo;

            case BuildConfiguration::Release:
                return ScriptModuleBuildConfiguration::Release;
            }

            return ScriptModuleBuildConfiguration::Debug;
        }

        [[nodiscard]] Result save_project_settings(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectPath,
            const ProjectSettings& a_settings) noexcept
        {
            const Core::IO::Path projectFilePath = Core::IO::Path::join(
                a_projectPath, Core::IO::Path("cueproject.json"));
            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(projectFilePath, &fileData);
            if (!result)
            {
                return result;
            }

            try
            {
                const std::string text(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
                nlohmann::json root = nlohmann::json::parse(text);
                root["scriptBuildConfiguration"] =
                    BuildSystem::to_configuration_name(
                        a_settings.scriptBuildConfiguration);
                root["scriptLoadConfiguration"] =
                    BuildSystem::to_configuration_name(
                        a_settings.scriptLoadConfiguration);
                root["scriptBuildBackend"] =
                    to_build_backend_name(a_settings.scriptBuildBackend);
                root["gameReleaseBuildConfiguration"] =
                    BuildSystem::to_configuration_name(
                        a_settings.gameReleaseBuildConfiguration);
                root["gameReleaseBuildBackend"] =
                    to_build_backend_name(a_settings.gameReleaseBuildBackend);
                root["gameReleaseOutputRoot"] =
                    a_settings.gameReleaseOutputRoot;
                root["startupScene"] = a_settings.startupScene;

                std::string updatedText = root.dump(4);
                updatedText.push_back('\n');
                const std::span<const char> textSpan(
                    updatedText.data(), updatedText.size());
                const std::span<const std::byte> byteSpan =
                    std::as_bytes(textSpan);
                return a_fileSystem.write_all(projectFilePath, byteSpan, false);
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "cueproject.json could not be updated.");
            }
        }

        [[nodiscard]] Result load_project_settings(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectPath,
            ProjectSettings& a_outSettings) noexcept
        {
            const Core::IO::Path projectFilePath = Core::IO::Path::join(
                a_projectPath, Core::IO::Path("cueproject.json"));
            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(projectFilePath, &fileData);
            if (!result)
            {
                return result;
            }

            try
            {
                const std::string text(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
                const nlohmann::json root = nlohmann::json::parse(text);

                a_outSettings.startupScene =
                    root.at("startupScene").get<std::string>();
                if (a_outSettings.startupScene.empty())
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Project startup scene is empty.");
                }

                a_outSettings.assetRoot =
                    root.value("assetRoot", std::string("Assets"));
                if (a_outSettings.assetRoot.empty())
                {
                    a_outSettings.assetRoot = "Assets";
                }

                a_outSettings.scriptRoot =
                    root.value("scriptRoot", std::string("."));
                if (a_outSettings.scriptRoot.empty())
                {
                    a_outSettings.scriptRoot = ".";
                }

                const std::string buildConfigurationText =
                    root.value("scriptBuildConfiguration", std::string("Debug"));
                result = parse_build_configuration(
                    buildConfigurationText,
                    a_outSettings.scriptBuildConfiguration);
                if (!result)
                {
                    return result;
                }

                const std::string loadConfigurationText =
                    root.value("scriptLoadConfiguration", buildConfigurationText);
                result = parse_build_configuration(
                    loadConfigurationText,
                    a_outSettings.scriptLoadConfiguration);
                if (!result)
                {
                    return result;
                }

                const std::string buildBackendText =
                    root.value("scriptBuildBackend", std::string("CMake"));
                result = parse_build_backend(
                    buildBackendText,
                    a_outSettings.scriptBuildBackend);
                if (!result)
                {
                    return result;
                }

                const std::string gameReleaseConfigurationText =
                    root.value("gameReleaseBuildConfiguration",
                        std::string("Release"));
                result = parse_build_configuration(
                    gameReleaseConfigurationText,
                    a_outSettings.gameReleaseBuildConfiguration);
                if (!result)
                {
                    return result;
                }

                const std::string gameReleaseBackendText =
                    root.value("gameReleaseBuildBackend", std::string("CMake"));
                result = parse_build_backend(
                    gameReleaseBackendText,
                    a_outSettings.gameReleaseBuildBackend);
                if (!result)
                {
                    return result;
                }

                a_outSettings.gameReleaseOutputRoot =
                    root.value("gameReleaseOutputRoot", std::string("Builds/Windows"));
                if (a_outSettings.gameReleaseOutputRoot.empty())
                {
                    a_outSettings.gameReleaseOutputRoot = "Builds/Windows";
                }

                return Result::ok();
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "cueproject.json could not be parsed.");
            }
        }
    }

    void EditorManager::initialize()
    {
        if (m_fileSystem != nullptr)
        {
            m_buildSystem = std::make_unique<BuildSystem>(*m_fileSystem);
            m_visualStudioBridge =
                std::make_unique<VisualStudioBridge>(*m_fileSystem);
            m_assetBrowser = std::make_unique<AssetBrowser>(m_fileSystem);
        }
        m_statistics =
            std::make_unique<Statistics>(m_engine->frame_controller(), *m_engine);
        m_statistics->set_update_metrics_source(&m_lastUpdateMetrics);
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend, &m_debugCamera);
        m_hierarchy = std::make_unique<Hierarchy>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId);
        m_inspector = std::make_unique<Inspector>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId, m_engine,
            m_fileSystem);
    }

    void EditorManager::set_loop_metrics_source(
        const EditorLoopMetrics* a_loopMetrics) noexcept
    {
        if (m_statistics != nullptr)
        {
            m_statistics->set_loop_metrics_source(a_loopMetrics);
        }
    }

    Result EditorManager::open_project(const std::string& a_projectPath)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        Result result = stop_play_mode();
        if (!result)
        {
            return result;
        }

        ProjectSettings projectSettings{};
        result = load_project_settings(
            *m_fileSystem, Core::IO::Path(a_projectPath), projectSettings);
        if (!result)
        {
            set_status_message(
                "cueproject.json の読み込みに失敗しました。", true);
            return result;
        }

        Core::IO::Path scenePath(projectSettings.startupScene);
        if (!scenePath.is_absolute())
        {
            scenePath = Core::IO::Path::join(Core::IO::Path(a_projectPath), scenePath);
        }

        Core::IO::Path scriptRootPath(projectSettings.scriptRoot);
        if (!scriptRootPath.is_absolute())
        {
            scriptRootPath =
                Core::IO::Path::join(Core::IO::Path(a_projectPath), scriptRootPath);
        }

        m_projectPath = a_projectPath;
        m_currentScenePath = scenePath.utf8();
        m_scriptBuildConfiguration = projectSettings.scriptBuildConfiguration;
        m_scriptLoadConfiguration = projectSettings.scriptLoadConfiguration;
        m_scriptBuildBackend = projectSettings.scriptBuildBackend;
        m_gameReleaseBuildConfiguration =
            projectSettings.gameReleaseBuildConfiguration;
        m_gameReleaseBuildBackend =
            projectSettings.gameReleaseBuildBackend;

        Core::IO::Path assetRootPath(projectSettings.assetRoot);
        if (!assetRootPath.is_absolute())
        {
            assetRootPath = Core::IO::Path::join(
                Core::IO::Path(a_projectPath), assetRootPath);
        }
        if (m_assetBrowser != nullptr)
        {
            m_assetBrowser->set_asset_root_path(assetRootPath);
        }
        if (m_inspector != nullptr)
        {
            m_inspector->set_asset_root_path(assetRootPath);
        }
        m_engine->set_asset_root_path(assetRootPath);

        const Result scriptLoadResult = m_engine->load_script_module(
            scriptRootPath,
            to_script_module_build_configuration(m_scriptLoadConfiguration));
        if (!scriptLoadResult && scriptLoadResult.code != Code::NotFound)
        {
            set_status_message("GameScript.dll の読み込みに失敗しました。", true);
            return scriptLoadResult;
        }

        result = reload_current_scene();
        if (!result)
        {
            set_status_message("スタートアップシーンの読み込みに失敗しました。",
                true);
            return result;
        }

        if (!scriptLoadResult && scriptLoadResult.code == Code::NotFound)
        {
            set_status_message(
                "プロジェクトを開きました。GameScript.dll はまだ見つかっていません。",
                true);
        }
        else
        {
            set_status_message("プロジェクトを開きました。", false);
        }
        return Result::ok();
    }

    Result EditorManager::save_current_scene()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }
        if (m_currentSceneId == GameCore::k_invalidSceneId ||
            m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no loaded scene to save.");
        }

        Result result = drain_pending_editor_commands();
        if (!result)
        {
            return result;
        }

        result = m_engine->game_world()->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        const std::string sceneName = !m_loadedSceneAsset.name().empty()
            ? m_loadedSceneAsset.name()
            : Core::IO::Path(m_currentScenePath).stem();
        GameCore::SceneAsset sceneAsset(sceneName);
        Result captureResult = Result::ok();
        result = m_engine->game_world()->for_each_object_in_scene(
            m_currentSceneId,
            [this, &sceneAsset, &captureResult](GameCore::EntityId a_entityId,
                GameCore::SceneId, GameCore::GameObject&)
            {
                if (!captureResult)
                {
                    return;
                }

                GameCore::DeletedObjectSnapshot snapshot{};
                captureResult = m_engine->game_world()->capture_deleted_object(
                    a_entityId, snapshot);
                if (!captureResult)
                {
                    return;
                }

                sceneAsset.add_object(std::move(snapshot.definition));
            });
        if (!result)
        {
            return result;
        }
        if (!captureResult)
        {
            return captureResult;
        }

        GameCore::SceneSerializer::SaveOptions saveOptions{};
        saveOptions.shouldSerializeScriptField = &should_serialize_script_field;
        saveOptions.userData = m_engine;
        saveOptions.assetManager = &m_engine->asset_manager();

        result = GameCore::SceneSerializer::save_scene_asset(
            sceneAsset,
            *m_fileSystem,
            Core::IO::Path(m_currentScenePath),
            saveOptions);
        if (!result)
        {
            return result;
        }

        m_loadedSceneAsset = std::move(sceneAsset);
        set_status_message("シーンを保存しました。", false);
        return Result::ok();
    }

    Result EditorManager::resolve_script_root(
        Core::IO::Path& a_outScriptRoot) const
    {
        a_outScriptRoot = {};

        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings projectSettings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
        if (!result)
        {
            return result;
        }

        a_outScriptRoot = Core::IO::Path(projectSettings.scriptRoot);
        if (!a_outScriptRoot.is_absolute())
        {
            a_outScriptRoot = Core::IO::Path::join(
                Core::IO::Path(m_projectPath), a_outScriptRoot);
        }

        return Result::ok();
    }

    Result EditorManager::build_script_module()
    {
        if (m_buildSystem == nullptr || m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem dependencies are not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        const ScriptBuildRequest request{
            scriptRoot,
            "win-x64",
            m_scriptBuildConfiguration,
            "GameScript",
            m_scriptBuildBackend
        };

        ScriptBuildValidation validation{};
        result = m_buildSystem->validate_script_build_environment(
            request,
            validation);
        if (!result)
        {
            return result;
        }

        m_lastScriptBuildResult = {};
        result = m_buildSystem->execute_script_build(
            request, m_lastScriptBuildResult);

        for (const BuildStageResult& stageResult : m_lastScriptBuildResult.stageResults)
        {
            log_build_output(
                to_stage_prefix(stageResult.stage),
                stageResult.output);
        }

        if (!result)
        {
            return result;
        }

        result = reload_script_module(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Reload)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::build_game_release()
    {
        if (m_buildSystem == nullptr || m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem dependencies are not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings projectSettings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path projectRoot(m_projectPath);
        const Core::IO::Path engineRoot(CUE_PROJECT_ROOT_PATH);
        const GameReleaseBuildRequest engineRequest{
            engineRoot,
            "win-x64",
            m_gameReleaseBuildConfiguration,
            "",
            "CueApp",
            m_gameReleaseBuildBackend
        };
        const GameReleaseBuildRequest projectRequest{
            projectRoot,
            "win-x64",
            m_gameReleaseBuildConfiguration,
            "Game",
            "CueApp",
            m_gameReleaseBuildBackend
        };

        GameReleaseBuildValidation validation{};
        result = m_buildSystem->validate_game_release_build_environment(
            engineRequest,
            validation);
        if (!result)
        {
            return result;
        }

        result = m_buildSystem->validate_game_release_build_environment(
            projectRequest,
            validation);
        if (!result)
        {
            return result;
        }

        m_lastGameReleaseBuildResult = {};
        m_lastGameReleaseBuildResult.succeeded = true;

        GameReleaseBuildResult gameBuildResult{};
        result = m_buildSystem->execute_game_release_build(
            engineRequest,
            gameBuildResult);
        append_game_release_result(m_lastGameReleaseBuildResult, gameBuildResult);
        for (const BuildStageResult& stageResult : gameBuildResult.stageResults)
        {
            log_build_output("[GameRelease]", stageResult.output);
        }
        if (!result)
        {
            return result;
        }

        GameReleaseBuildResult appBuildResult{};
        result = m_buildSystem->execute_game_release_build(
            projectRequest,
            appBuildResult);
        append_game_release_result(m_lastGameReleaseBuildResult, appBuildResult);
        for (const BuildStageResult& stageResult : appBuildResult.stageResults)
        {
            log_build_output("[GameRelease]", stageResult.output);
        }
        if (!result)
        {
            return result;
        }

        const Core::IO::Path stagingDirectory =
            resolve_game_release_output_directory(projectRoot, projectSettings);
        result = remove_path_recursive(*m_fileSystem, stagingDirectory);
        if (!result)
        {
            return result;
        }

        result = m_fileSystem->create_directories(stagingDirectory);
        if (!result)
        {
            return result;
        }

        const char* configurationName =
            BuildSystem::to_configuration_name(m_gameReleaseBuildConfiguration);
        const Core::IO::Path engineAppOutputDirectory = Core::IO::Path::join(
            engineRoot,
            Core::IO::Path(std::string("generated/outputs/App/") +
                configurationName));
        const Core::IO::Path projectOutputDirectory = Core::IO::Path::join(
            projectRoot,
            Core::IO::Path(std::string("out/build/win-x64/") +
                configurationName));
        const Core::IO::Path assetRoot = Core::IO::Path::join(
            projectRoot,
            Core::IO::Path(projectSettings.assetRoot));
        const Core::IO::Path cueProjectFile = Core::IO::Path::join(
            projectRoot,
            Core::IO::Path("cueproject.json"));

        const Core::IO::Path projectCueAppPath = Core::IO::Path::join(
            projectOutputDirectory,
            Core::IO::Path("CueApp.exe"));
        result = m_fileSystem->copy_file(
            projectCueAppPath,
            Core::IO::Path::join(stagingDirectory, Core::IO::Path("CueApp.exe")),
            true);
        if (!result)
        {
            return result;
        }

        const std::array<std::string, 2> engineFiles = {
            "dxcompiler.dll",
            "dxil.dll"
        };
        for (const std::string& fileName : engineFiles)
        {
            const Core::IO::Path sourcePath = Core::IO::Path::join(
                engineAppOutputDirectory,
                Core::IO::Path(fileName));
            const Core::IO::Path destinationPath = Core::IO::Path::join(
                stagingDirectory,
                Core::IO::Path(fileName));

            bool exists = false;
            result = m_fileSystem->exists(sourcePath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                continue;
            }

            result = m_fileSystem->copy_file(sourcePath, destinationPath, true);
            if (!result)
            {
                return result;
            }
        }

        result = copy_directory_recursive(
            *m_fileSystem,
            Core::IO::Path::join(
                engineAppOutputDirectory,
                Core::IO::Path("EngineResources")),
            Core::IO::Path::join(stagingDirectory, Core::IO::Path("EngineResources")));
        if (!result)
        {
            return result;
        }

        result = copy_directory_recursive(
            *m_fileSystem,
            Core::IO::Path::join(engineAppOutputDirectory, Core::IO::Path("config")),
            Core::IO::Path::join(stagingDirectory, Core::IO::Path("config")));
        if (!result)
        {
            return result;
        }

        result = cook_project_sounds(*m_fileSystem, projectRoot, projectSettings);
        if (!result)
        {
            return result;
        }

        result = copy_release_assets_recursive(
            *m_fileSystem,
            assetRoot,
            Core::IO::Path::join(stagingDirectory, Core::IO::Path("Assets")));
        if (!result)
        {
            return result;
        }

        result = m_fileSystem->copy_file(
            cueProjectFile,
            Core::IO::Path::join(stagingDirectory, Core::IO::Path("cueproject.json")),
            true);
        if (!result)
        {
            return result;
        }

        m_lastGameReleaseBuildResult.artifacts.push_back(BuildArtifact{
            "GameReleasePackage",
            stagingDirectory
        });
        m_lastGameReleaseBuildResult.summary =
            "ゲーム Release 配布フォルダを作成しました。";
        m_lastGameReleaseBuildResult.succeeded = true;

        return result;
    }

    Result EditorManager::open_script_solution_in_visual_studio()
    {
        if (m_visualStudioBridge == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "VisualStudioBridge is not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        return m_visualStudioBridge->open_solution(scriptRoot, "win-x64");
    }

    Result EditorManager::attach_editor_debugger_in_visual_studio()
    {
        if (m_visualStudioBridge == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "VisualStudioBridge is not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        return m_visualStudioBridge->attach_debugger(
            scriptRoot, "win-x64", ::GetCurrentProcessId());
    }

    Result EditorManager::open_game_release_build_directory()
    {
        if (m_buildSystem == nullptr || m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem が初期化されていません。");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "プロジェクトが開かれていません。");
        }

        ProjectSettings projectSettings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
        if (!result)
        {
            return result;
        }

        return open_path_in_shell(
            resolve_game_release_output_directory(
                Core::IO::Path(m_projectPath),
                projectSettings));
    }

    Result EditorManager::open_path_in_shell(
        const Core::IO::Path& a_path) const
    {
        if (a_path.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Open path is empty.");
        }

        std::wstring widePath{};
        Result result = PAL::Win::utf8_to_wide(a_path.utf8(), &widePath);
        if (!result)
        {
            return result;
        }

        const HINSTANCE executeResult = ::ShellExecuteW(
            nullptr,
            L"open",
            widePath.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(executeResult) <= 32)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Shell でパスを開けませんでした。");
        }

        return Result::ok();
    }

    Result EditorManager::reload_script_module()
    {
        m_lastScriptBuildResult = {};
        Result result = reload_script_module(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Reload)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::create_script_template(
        const std::string& a_scriptName)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "FileSystem が初期化されていません。");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "プロジェクトが開かれていません。");
        }

        ProjectGenerator projectGenerator(*m_fileSystem);
        Result result = projectGenerator.create_script_template(
            m_projectPath,
            a_scriptName);
        if (!result)
        {
            return result;
        }

        BuildResult configureResult{};
        result = refresh_script_project_intellisense(configureResult);
        m_lastScriptBuildResult = std::move(configureResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Configure)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::refresh_script_project_intellisense(
        BuildResult& a_outResult)
    {
        a_outResult = {};

        if (m_buildSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem が初期化されていません。");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        const ScriptBuildRequest request{
            scriptRoot,
            "win-x64",
            m_scriptBuildConfiguration,
            "GameScript",
            m_scriptBuildBackend
        };
        return m_buildSystem->execute_script_configure(
            request,
            a_outResult);
    }

    Result EditorManager::reload_script_module(BuildResult& a_inOutBuildResult)
    {
        if (m_engine == nullptr)
        {
            const Result result = Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
            a_inOutBuildResult.stageResults.push_back(BuildStageResult{
                BuildStage::Reload,
                "Engine::load_script_module",
                std::string(result.message),
                {},
                1,
                false
            });
            a_inOutBuildResult.summary = std::string(result.message);
            a_inOutBuildResult.exitCode = 1;
            a_inOutBuildResult.succeeded = false;
            push_build_message(a_inOutBuildResult, BuildMessageSeverity::Error,
                BuildStage::Reload, std::string(result.message));
            return result;
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            a_inOutBuildResult.stageResults.push_back(BuildStageResult{
                BuildStage::Reload,
                "Engine::load_script_module",
                std::string(result.message),
                {},
                1,
                false
            });
            a_inOutBuildResult.summary = std::string(result.message);
            a_inOutBuildResult.exitCode = 1;
            a_inOutBuildResult.succeeded = false;
            push_build_message(a_inOutBuildResult, BuildMessageSeverity::Error,
                BuildStage::Reload, std::string(result.message));
            return result;
        }

        result = m_engine->load_script_module(
            scriptRoot,
            to_script_module_build_configuration(m_scriptLoadConfiguration));
        const bool reloadSucceeded = static_cast<bool>(result);
        const bool hasBuildStage =
            has_stage_result(a_inOutBuildResult, BuildStage::Build) ||
            has_stage_result(a_inOutBuildResult, BuildStage::Configure);
        const std::string reloadOutput = reloadSucceeded
            ? std::string("GameScript の再読み込みに成功しました。")
            : std::string(result.message);

        a_inOutBuildResult.stageResults.push_back(BuildStageResult{
            BuildStage::Reload,
            "Engine::load_script_module",
            reloadOutput,
            {},
            reloadSucceeded ? 0u : 1u,
            reloadSucceeded
        });
        a_inOutBuildResult.exitCode = reloadSucceeded ? 0u : 1u;
        a_inOutBuildResult.succeeded =
            hasBuildStage ? (a_inOutBuildResult.succeeded && reloadSucceeded)
                          : reloadSucceeded;
        a_inOutBuildResult.summary = reloadSucceeded
            ? (hasBuildStage
                ? "GameScript のビルドと再読み込みに成功しました。"
                : "GameScript の再読み込みに成功しました。")
            : (hasBuildStage
                ? "GameScript のビルド後の再読み込みに失敗しました。"
                : "GameScript の再読み込みに失敗しました。");
        push_build_message(
            a_inOutBuildResult,
            reloadSucceeded ? BuildMessageSeverity::Info
                            : BuildMessageSeverity::Error,
            BuildStage::Reload,
            reloadOutput);

        if (reloadSucceeded)
        {
            const ScriptModuleHost::ScriptReloadReport& reloadReport =
                m_engine->last_script_reload_report();
            if (reloadReport.skippedStateCount > 0)
            {
                push_build_message(
                    a_inOutBuildResult,
                    BuildMessageSeverity::Warning,
                    BuildStage::Reload,
                    std::string("state restore skipped: ") +
                        std::to_string(reloadReport.skippedStateCount) +
                        " 件");
                for (const std::string& warning : reloadReport.warnings)
                {
                    push_build_message(
                        a_inOutBuildResult,
                        BuildMessageSeverity::Warning,
                        BuildStage::Reload,
                        warning);
                }
            }
        }

        return result;
    }

    Result EditorManager::save_script_build_configuration(
        BuildConfiguration a_configuration)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.scriptBuildConfiguration = a_configuration;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_scriptBuildConfiguration = a_configuration;
        return Result::ok();
    }

    Result EditorManager::save_script_load_configuration(
        BuildConfiguration a_configuration)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.scriptLoadConfiguration = a_configuration;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_scriptLoadConfiguration = a_configuration;
        return Result::ok();
    }

    Result EditorManager::save_script_build_backend(
        BuildBackend a_backend)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.scriptBuildBackend = a_backend;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_scriptBuildBackend = a_backend;
        return Result::ok();
    }

    Result EditorManager::save_game_release_build_configuration(
        BuildConfiguration a_configuration)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.gameReleaseBuildConfiguration = a_configuration;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_gameReleaseBuildConfiguration = a_configuration;
        return Result::ok();
    }

    Result EditorManager::save_game_release_build_backend(
        BuildBackend a_backend)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.gameReleaseBuildBackend = a_backend;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_gameReleaseBuildBackend = a_backend;
        return Result::ok();
    }

    void EditorManager::queue_script_action(PendingScriptAction a_action)
    {
        if (a_action == PendingScriptAction::None)
        {
            return;
        }

        m_pendingScriptAction = a_action;
        m_pendingScriptActionDelayFrames = 1;
        m_isScriptActionActive = true;

        switch (a_action)
        {
        case PendingScriptAction::Reload:
            set_status_message("GameScript を再読み込みしています...", false);
            break;

        case PendingScriptAction::Build:
            set_status_message("GameScript をビルドしています...", false);
            break;

        case PendingScriptAction::None:
            break;
        }
    }

    void EditorManager::process_pending_script_action()
    {
        if (m_pendingScriptAction == PendingScriptAction::None)
        {
            m_isScriptActionActive = false;
            return;
        }

        if (m_pendingScriptActionDelayFrames > 0)
        {
            --m_pendingScriptActionDelayFrames;
            return;
        }

        const PendingScriptAction action = m_pendingScriptAction;
        m_pendingScriptAction = PendingScriptAction::None;

        Result result = Result::ok();
        switch (action)
        {
        case PendingScriptAction::Reload:
            result = reload_script_module();
            if (!result)
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                log_result("Failed to reload GameScript", result);
                set_status_message(
                    detail.empty()
                    ? "GameScript の再読み込みに失敗しました。"
                    : "GameScript の再読み込みに失敗しました: " + detail,
                    true);
                set_script_build_notification(
                    "GameScript Reload Failed",
                    detail.empty() ? std::string(result.message) : detail,
                    true,
                    true);
                m_showScriptBuildOutput = true;
            }
            else
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                set_status_message(
                    detail.empty()
                    ? "GameScript を再読み込みしました。"
                    : "GameScript を再読み込みしました: " + detail,
                    false);
                set_script_build_notification(
                    "GameScript Reload Succeeded",
                    detail.empty()
                    ? "GameScript の再読み込みに成功しました。"
                    : detail,
                    false,
                    false);
            }
            break;

        case PendingScriptAction::Build:
            result = build_script_module();
            if (!result)
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                log_result("Failed to build GameScript", result);
                set_status_message(
                    detail.empty()
                    ? "GameScript のビルドに失敗しました。"
                    : "GameScript のビルドに失敗しました: " + detail,
                    true);
                set_script_build_notification(
                    "GameScript Build Failed",
                    detail.empty() ? std::string(result.message) : detail,
                    true,
                    true);
                m_showScriptBuildOutput = true;
            }
            else
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                set_status_message(
                    detail.empty()
                    ? "GameScript をビルドして再読み込みしました。"
                    : "GameScript をビルドして再読み込みしました: " + detail,
                    false);
                set_script_build_notification(
                    "GameScript Build Succeeded",
                    detail.empty()
                    ? "GameScript のビルドと再読み込みに成功しました。"
                    : detail,
                    false,
                    false);
            }
            break;

        case PendingScriptAction::None:
            break;
        }

        m_isScriptActionActive = false;
    }

    Result EditorManager::reload_current_scene()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }
        if (m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no scene path to load.");
        }

        Result result = drain_pending_editor_commands();
        if (!result)
        {
            return result;
        }

        result = unload_current_scene();
        if (!result)
        {
            return result;
        }

        ProjectSettings projectSettings{};
        result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
        if (!result)
        {
            return result;
        }

        result = load_project_textures(
            *m_engine,
            *m_fileSystem,
            Core::IO::Path(m_projectPath),
            projectSettings);
        if (!result)
        {
            return result;
        }

        result = load_project_models(
            *m_engine,
            *m_fileSystem,
            Core::IO::Path(m_projectPath),
            projectSettings);
        if (!result)
        {
            return result;
        }

        result = load_project_materials(
            *m_engine,
            *m_fileSystem,
            Core::IO::Path(m_projectPath),
            projectSettings);
        if (!result)
        {
            return result;
        }

        result = cook_project_sounds(
            *m_fileSystem,
            Core::IO::Path(m_projectPath),
            projectSettings);
        if (!result)
        {
            return result;
        }

        GameCore::SceneAsset sceneAsset{};
        GameCore::SceneSerializer::LoadOptions loadOptions{};
        loadOptions.assetManager = &m_engine->asset_manager();
        result = GameCore::SceneSerializer::load_scene_asset(
            *m_fileSystem, Core::IO::Path(m_currentScenePath), sceneAsset, loadOptions);
        if (!result)
        {
            return result;
        }

        m_loadedSceneAsset = std::move(sceneAsset);

        GameCore::GameWorld::LoadSceneResult loadResult{};
        result = m_engine->game_world()->load_scene(m_loadedSceneAsset, loadResult);
        if (!result)
        {
            return result;
        }

        m_currentSceneId = loadResult.sceneId;
        m_engine->set_editor_scene_id(m_currentSceneId);
        m_selectedEntityId = GameCore::k_invalidEntityId;
        set_status_message("シーンを読み込みました。", false);
        return Result::ok();
    }

    Result EditorManager::unload_current_scene()
    {
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        m_engine->set_editor_scene_id(GameCore::k_invalidSceneId);
        if (m_currentSceneId == GameCore::k_invalidSceneId)
        {
            return Result::ok();
        }

        const GameCore::SceneId sceneId = m_currentSceneId;
        Result result = m_engine->game_world()->unload_scene(sceneId);
        if (!result)
        {
            return result;
        }

        result = m_engine->game_world()->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        m_currentSceneId = GameCore::k_invalidSceneId;
        m_loadedSceneAsset = {};
        m_selectedEntityId = GameCore::k_invalidEntityId;
        return Result::ok();
    }

    Result EditorManager::drain_pending_editor_commands()
    {
        if (m_bridge == nullptr)
        {
            return Result::ok();
        }
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        return m_bridge->drain_commands(commandContext);
    }

    void EditorManager::set_status_message(std::string a_message, bool a_isError)
    {
        m_statusMessage = std::move(a_message);
        m_hasStatusError = a_isError;
    }

    void EditorManager::set_script_build_notification(
        std::string a_title,
        std::string a_message,
        bool a_isError,
        bool a_openPopup)
    {
        m_scriptBuildNotificationTitle = std::move(a_title);
        m_scriptBuildNotificationMessage = std::move(a_message);
        m_hasScriptBuildNotification = true;
        m_hasScriptBuildNotificationError = a_isError;
        m_openScriptBuildNotificationPopup = a_openPopup;
    }

    Result EditorManager::start_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        return m_engine->start_play_mode();
    }

    Result EditorManager::stop_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        return m_engine->stop_play_mode();
    }

    Result EditorManager::exit_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        if (!m_engine->is_playing())
        {
            return Result::ok();
        }

        return m_engine->stop_play_mode();
    }

    void EditorManager::draw_script_build_output()
    {
        if (!m_showScriptBuildOutput)
        {
            return;
        }

        if (!ImGui::Begin("Script Build Output", &m_showScriptBuildOutput))
        {
            ImGui::End();
            return;
        }

        const bool hasBuildResult =
            !m_lastScriptBuildResult.summary.empty() ||
            !m_lastScriptBuildResult.stageResults.empty() ||
            !m_lastScriptBuildResult.messages.empty() ||
            !m_lastScriptBuildResult.artifacts.empty();

        if (!hasBuildResult)
        {
            ImGui::TextUnformatted(
                "まだ GameScript build は実行されていません。");
            ImGui::End();
            return;
        }

        const ImVec4 successColor = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
        const ImVec4 errorColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        const ImVec4 warningColor = ImVec4(0.95f, 0.75f, 0.30f, 1.0f);

        ImGui::Text("Summary");
        ImGui::Separator();
        ImGui::Text("Result: ");
        ImGui::SameLine();
        ImGui::TextColored(
            m_lastScriptBuildResult.succeeded ? successColor : errorColor,
            m_lastScriptBuildResult.succeeded ? "Succeeded" : "Failed");
        const std::string primaryMessage =
            make_primary_build_message(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.summary.empty())
        {
            ImGui::TextWrapped("%s", m_lastScriptBuildResult.summary.c_str());
        }
        if (!primaryMessage.empty() &&
            primaryMessage != m_lastScriptBuildResult.summary)
        {
            ImGui::TextColored(
                m_lastScriptBuildResult.succeeded ? successColor : errorColor,
                "Primary: %s",
                primaryMessage.c_str());
        }
        ImGui::Text("Exit Code: %u", m_lastScriptBuildResult.exitCode);
        ImGui::Text("Did Configure: %s",
            m_lastScriptBuildResult.didConfigure ? "Yes" : "No");

        if (ImGui::Button("Open Solution"))
        {
            const Result result = open_script_solution_in_visual_studio();
            if (!result)
            {
                log_result("Failed to open GameScript solution", result);
                set_status_message(
                    "GameScript solution を開けませんでした。", true);
            }
            else
            {
                set_status_message(
                    "GameScript solution を Visual Studio で開きました。",
                    false);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Attach Editor"))
        {
            const Result result = attach_editor_debugger_in_visual_studio();
            if (!result)
            {
                log_result("Failed to attach debugger", result);
                set_status_message(
                    "Visual Studio から Editor にアタッチできませんでした。",
                    true);
            }
            else
            {
                set_status_message(
                    "Visual Studio から Editor にアタッチしました。",
                    false);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Output"))
        {
            m_lastScriptBuildResult = {};
            ImGui::End();
            return;
        }

        const auto draw_path_row =
            [this](const char* a_label, const Core::IO::Path& a_path)
        {
            if (a_path.is_empty())
            {
                return;
            }

            ImGui::Text("%s", a_label);
            ImGui::SameLine();
            ImGui::PushItemWidth(-80.0f);
            std::string pathText = a_path.utf8();
            ImGui::InputText(
                (std::string("##") + a_label).c_str(),
                pathText.data(),
                pathText.size() + 1,
                ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button((std::string("Copy##") + a_label).c_str()))
            {
                ImGui::SetClipboardText(pathText.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string("Open##") + a_label).c_str()))
            {
                const Result result = open_path_in_shell(a_path);
                if (!result)
                {
                    log_result("Failed to open path", result);
                    set_status_message(
                        std::string(a_label) + " を開けませんでした。", true);
                }
                else
                {
                    set_status_message(
                        std::string(a_label) + " を開きました。", false);
                }
            }
        };

        draw_path_row("Configure Log", m_lastScriptBuildResult.configureLogPath);
        draw_path_row("Build Log", m_lastScriptBuildResult.buildLogPath);

        ImGui::Spacing();
        ImGui::Text("Stages");
        ImGui::Separator();
        if (m_lastScriptBuildResult.stageResults.empty())
        {
            ImGui::TextUnformatted("stage result はありません。");
        }
        else
        {
            for (size_t index = 0;
                 index < m_lastScriptBuildResult.stageResults.size();
                 ++index)
            {
                const BuildStageResult& stageResult =
                    m_lastScriptBuildResult.stageResults[index];
                const ImVec4 stageColor =
                    stageResult.succeeded ? successColor : errorColor;
                const std::string stageLabel =
                    "[" + std::string(to_stage_name(stageResult.stage)) + "] " +
                    (stageResult.succeeded ? "Succeeded" : "Failed") +
                    "##stage" + std::to_string(index);

                if (ImGui::TreeNodeEx(
                        stageLabel.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Stage: %s", to_stage_name(stageResult.stage));
                    ImGui::SameLine();
                    ImGui::TextColored(
                        stageColor,
                        "%s",
                        stageResult.succeeded ? "Succeeded" : "Failed");
                    ImGui::Text("Exit Code: %u", stageResult.exitCode);
                    if (!stageResult.command.empty())
                    {
                        ImGui::TextWrapped("Command: %s",
                            stageResult.command.c_str());
                    }
                    if (!stageResult.logPath.is_empty())
                    {
                        draw_path_row("Stage Log", stageResult.logPath);
                    }
                    if (!stageResult.output.empty())
                    {
                        ImGui::TextUnformatted("Output");
                        ImGui::BeginChild(
                            (std::string("StageOutput##") + std::to_string(index)).c_str(),
                            ImVec2(0.0f, 120.0f),
                            true);
                        ImGui::TextUnformatted(stageResult.output.c_str());
                        ImGui::EndChild();
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Spacing();
        ImGui::Text("Messages");
        ImGui::Separator();
        if (m_lastScriptBuildResult.messages.empty())
        {
            ImGui::TextUnformatted("message はありません。");
        }
        else
        {
            ImGui::BeginChild("BuildMessages", ImVec2(0.0f, 140.0f), true);
            for (const BuildMessage& message : m_lastScriptBuildResult.messages)
            {
                ImVec4 color = successColor;
                if (message.severity == BuildMessageSeverity::Warning)
                {
                    color = warningColor;
                }
                else if (message.severity == BuildMessageSeverity::Error)
                {
                    color = errorColor;
                }

                ImGui::TextColored(
                    color,
                    "[%s][%s]",
                    to_stage_name(message.stage),
                    to_severity_name(message.severity));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", message.text.c_str());
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Text("Artifacts");
        ImGui::Separator();
        if (m_lastScriptBuildResult.artifacts.empty())
        {
            ImGui::TextUnformatted("artifact はありません。");
        }
        else
        {
            for (const BuildArtifact& artifact : m_lastScriptBuildResult.artifacts)
            {
                ImGui::Text("%s", artifact.name.c_str());
                draw_path_row("Path", artifact.path);
            }
        }

        ImGui::End();
    }

    void EditorManager::draw_script_build_notification_popup()
    {
        if (m_openScriptBuildNotificationPopup)
        {
            ImGui::OpenPopup("Script Build Notification");
            m_openScriptBuildNotificationPopup = false;
        }

        if (!ImGui::BeginPopupModal(
                "Script Build Notification",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const ImVec4 messageColor = m_hasScriptBuildNotificationError
            ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
            : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
        if (!m_scriptBuildNotificationTitle.empty())
        {
            ImGui::TextColored(
                messageColor,
                "%s",
                m_scriptBuildNotificationTitle.c_str());
            ImGui::Separator();
        }

        if (!m_scriptBuildNotificationMessage.empty())
        {
            ImGui::TextWrapped(
                "%s",
                m_scriptBuildNotificationMessage.c_str());
        }

        ImGui::Spacing();
        if (ImGui::Button("Build Output を開く"))
        {
            m_showScriptBuildOutput = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("閉じる"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::draw_create_script_popup()
    {
        if (m_openCreateScriptPopup)
        {
            ImGui::OpenPopup("Create Script");
            m_openCreateScriptPopup = false;
            m_focusCreateScriptNameInput = true;
        }

        if (!ImGui::BeginPopupModal(
                "Create Script",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Assets/Scripts/ に新しい Script を作成します。");
        ImGui::Spacing();
        ImGui::TextUnformatted("Script 名");
        if (m_focusCreateScriptNameInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusCreateScriptNameInput = false;
        }

        const bool submitted = ImGui::InputText(
            "##CreateScriptName",
            m_createScriptNameBuffer.data(),
            m_createScriptNameBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("例: TestCube");

        auto submit = [this]()
        {
            const std::string scriptName = m_createScriptNameBuffer.data();
            const Result result = create_script_template(scriptName);
            if (!result)
            {
                log_result("Failed to create script template", result);
                set_status_message(
                    result.message.empty()
                        ? "Script 作成に失敗しました。"
                        : std::string(result.message),
                    true);
                return;
            }

            m_createScriptNameBuffer.fill('\0');
            set_status_message(
                std::string("Script を作成しました: ") + scriptName,
                false);
            ImGui::CloseCurrentPopup();
        };

        ImGui::Spacing();
        if (submitted || ImGui::Button("作成"))
        {
            submit();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            m_createScriptNameBuffer.fill('\0');
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::undo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        Result result = m_bridge->undo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to undo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::redo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        Result result = m_bridge->redo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to redo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::handle_shortcuts()
    {
        if (m_bridge == nullptr)
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return;
        }

        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            undo_last_command();
        }

        if (io.KeyCtrl &&
            (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
        {
            redo_last_command();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            const Result result = save_current_scene();
            if (!result)
            {
                log_result("Failed to save scene", result);
                set_status_message("シーン保存に失敗しました。", true);
            }
        }
    }

    void EditorManager::draw_main_camera_menu()
    {
        const bool canSelectMainCamera = m_bridge != nullptr &&
            m_engine != nullptr && m_engine->game_world() != nullptr &&
            m_currentSceneId != GameCore::k_invalidSceneId &&
            !m_isScriptActionActive;
        if (!ImGui::BeginMenu("メインカメラ", canSelectMainCamera))
        {
            return;
        }

        std::vector<SceneCameraMenuEntry> cameras{};
        const Result collectResult = m_engine->game_world()->for_each_object_in_scene(
            m_currentSceneId,
            [&cameras](
                GameCore::EntityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
            {
                ECS::CameraComponent* camera = nullptr;
                if (!a_object.get_component(camera) || camera == nullptr)
                {
                    return;
                }

                SceneCameraMenuEntry entry{};
                entry.entityId = a_object.entity_id();
                entry.isMain = camera->isMain;
                Result nameResult = a_object.name(entry.name);
                if (!nameResult || entry.name.empty())
                {
                    entry.name = "Camera";
                }

                cameras.push_back(std::move(entry));
            });
        if (!collectResult)
        {
            ImGui::TextDisabled("Scene のカメラを取得できません。");
            ImGui::EndMenu();
            return;
        }

        if (cameras.empty())
        {
            ImGui::TextDisabled("Scene 内にカメラがありません。");
            ImGui::EndMenu();
            return;
        }

        for (size_t cameraIndex = 0; cameraIndex < cameras.size(); ++cameraIndex)
        {
            const SceneCameraMenuEntry& camera = cameras[cameraIndex];
            const std::string label =
                camera.name + "##MainCamera" + std::to_string(cameraIndex);
            if (ImGui::MenuItem(label.c_str(), nullptr, camera.isMain, true))
            {
                const Result result = m_bridge->submit_command(
                    std::make_unique<SetMainCameraCommand>(camera.entityId));
                if (!result)
                {
                    log_result("Failed to set main camera", result);
                    set_status_message(
                        "メインカメラの変更に失敗しました。", true);
                }
                else
                {
                    set_status_message("メインカメラを変更しました。", false);
                }
            }
        }

        ImGui::EndMenu();
    }

    void EditorManager::process_debug_pick_request()
    {
        if (m_debugView == nullptr || m_engine == nullptr)
        {
            return;
        }

        GameCore::EntityId pickedEntityId = GameCore::k_invalidEntityId;
        if (m_engine->consume_debug_pick_result(pickedEntityId))
        {
            m_selectedEntityId = pickedEntityId;
        }

        DebugView::PickRequest pickRequest{};
        if (!m_debugView->consume_pick_request(pickRequest))
        {
            return;
        }

        m_engine->request_debug_pick(
            pickRequest.normalizedX,
            pickRequest.normalizedY);
    }

    void EditorManager::sync_debug_selection()
    {
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        GpuData::DebugSelectionGpu selection{};
        uint32_t selectedObjectId = 0;
        constexpr float k_cameraFrustumNear = 0.03f;
        constexpr float k_cameraFrustumFar = 1.0f;
        auto appendDebugItem =
            [&selection](const GpuData::DebugSelectionItemGpu& a_item) noexcept
        {
            if (selection.itemCount >= GpuData::k_maxDebugSelectionItemCount)
            {
                return;
            }

            selection.items[selection.itemCount] = a_item;
            ++selection.itemCount;
        };
        auto makeCameraItem =
            [&](const ECS::TransformComponent& a_transform,
                const ECS::CameraComponent& a_camera,
                bool a_isSelected) noexcept
        {
            GpuData::DebugSelectionItemGpu item{};
            item.world = Math::make_affine_matrix(
                Math::float3(1.0f, 1.0f, 1.0f),
                a_transform.rotation,
                a_transform.position);
            item.color = a_isSelected
                ? Math::float4(1.0f, 0.84f, 0.18f, 1.0f)
                : Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
            item.camera = Math::float4(
                std::clamp(a_camera.fovY, 1.0f, 179.0f),
                a_camera.aspectRatio > 0.0f ? a_camera.aspectRatio : 1.0f,
                k_cameraFrustumNear,
                k_cameraFrustumFar);
            item.shape = static_cast<uint32_t>(
                GpuData::DebugSelectionShape::CameraFrustum);
            item.isEnabled = 1;
            return item;
        };
        if (m_selectedEntityId != GameCore::k_invalidEntityId)
        {
            const ECS::TransformComponent* transform = nullptr;
            const Result transformResult =
                m_engine->game_world()->get_component<ECS::TransformComponent>(
                    m_selectedEntityId, transform);
            const ECS::CameraComponent* camera = nullptr;
            if (transformResult)
            {
                (void)m_engine->game_world()->get_component<ECS::CameraComponent>(
                    m_selectedEntityId,
                    camera);
                if (camera == nullptr)
                {
                    GpuData::DebugSelectionItemGpu item{};
                    item.world = Math::make_affine_matrix(
                        transform->scale * 1.08f,
                        transform->rotation,
                        transform->position);
                    item.color = Math::float4(1.0f, 0.84f, 0.18f, 1.0f);
                    item.shape = static_cast<uint32_t>(
                        GpuData::DebugSelectionShape::Box);
                    item.isEnabled = 1;
                    appendDebugItem(item);
                }
            }

            const ECS::RenderableInfoComponent* renderableInfo = nullptr;
            if (m_engine->game_world()->get_component<ECS::RenderableInfoComponent>(
                    m_selectedEntityId, renderableInfo) &&
                renderableInfo->objectId != ECS::k_invalidRenderableId)
            {
                selectedObjectId = renderableInfo->objectId + 1u;
            }
        }

        auto appendCameraObject =
            [this, &appendDebugItem, &makeCameraItem](
                GameCore::EntityId a_entityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
        {
            ECS::TransformComponent* transform = nullptr;
            ECS::CameraComponent* camera = nullptr;
            if (!a_object.get_component(transform) || transform == nullptr ||
                !a_object.get_component(camera) || camera == nullptr)
            {
                return;
            }

            appendDebugItem(makeCameraItem(
                *transform,
                *camera,
                a_entityId == m_selectedEntityId));
        };
        bool hasCollectedSceneCameras = false;
        if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            const Result collectResult =
                m_engine->game_world()->for_each_object_in_scene(
                m_currentSceneId,
                appendCameraObject);
            hasCollectedSceneCameras = static_cast<bool>(collectResult);
        }
        if (!hasCollectedSceneCameras)
        {
            (void)m_engine->game_world()->for_each_object(appendCameraObject);
        }

        m_engine->set_debug_selection(selection);
        m_engine->set_debug_selected_object_id(selectedObjectId);
    }

    void EditorManager::update()
    {
        m_currentUpdateMetrics = EditorUpdateMetrics{};
        Core::Time::Timer updateTimer(m_platform->clock());
        updateTimer.start();

        Core::Time::Timer pendingTimer(m_platform->clock());
        pendingTimer.start();
        process_pending_script_action();
        pendingTimer.stop();
        m_currentUpdateMetrics.pendingScriptActionMs =
            pendingTimer.elapsed_ticks().ms_f64();

        // ビューポート全体をカバーするドックスペースを作成
        Core::Time::Timer dockspaceTimer(m_platform->clock());
        dockspaceTimer.start();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("DockSpace Window", nullptr, window_flags);
        ImGui::PopStyleVar(2);
        dockspaceTimer.stop();
        m_currentUpdateMetrics.dockspaceMs =
            dockspaceTimer.elapsed_ticks().ms_f64();

        static bool showMetricsWindow = false;
        static bool showDemoWindow = false;
        static bool showStyleEditor = false;

        handle_shortcuts();

        Core::Time::Timer menuBarTimer(m_platform->clock());
        menuBarTimer.start();
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("ファイル"))
            {
                const bool canOperateScene =
                    m_currentSceneId != GameCore::k_invalidSceneId &&
                    !m_currentScenePath.empty();

                if (ImGui::MenuItem("シーンを保存", "Ctrl+S", false, canOperateScene))
                {
                    const Result result = save_current_scene();
                    if (!result)
                    {
                        log_result("Failed to save scene", result);
                        set_status_message("シーン保存に失敗しました。", true);
                    }
                }

                if (ImGui::MenuItem("シーンを再読み込み", nullptr, false, canOperateScene))
                {
                    const Result result = reload_current_scene();
                    if (!result)
                    {
                        log_result("Failed to reload scene", result);
                        set_status_message("シーン再読み込みに失敗しました。", true);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("編集"))
            {
                const bool canUndo = m_bridge != nullptr && m_bridge->can_undo();
                const bool canRedo = m_bridge != nullptr && m_bridge->can_redo();
                const bool canAddObject =
                    m_bridge != nullptr && !m_isScriptActionActive;

                if (ImGui::BeginMenu("3D", canAddObject))
                {
                    if (ImGui::MenuItem("カメラを追加"))
                    {
                        const Result result = m_bridge->submit_command(
                            std::make_unique<AddObjectCommand>(
                                AddObjectType::Camera));
                        if (!result)
                        {
                            log_result("Failed to add camera object", result);
                            set_status_message(
                                "カメラの追加に失敗しました。", true);
                        }
                        else
                        {
                            set_status_message("カメラを追加しました。", false);
                        }
                    }

                    if (ImGui::MenuItem("オブジェクトを追加"))
                    {
                        const Result result = m_bridge->submit_command(
                            std::make_unique<AddObjectCommand>(
                                AddObjectType::StaticMesh3D));
                        if (!result)
                        {
                            log_result("Failed to add 3D object", result);
                            set_status_message(
                                "3D オブジェクトの追加に失敗しました。", true);
                        }
                        else
                        {
                            set_status_message(
                                "3D オブジェクトを追加しました。", false);
                        }
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("2D", canAddObject))
                {
                    if (ImGui::MenuItem("オブジェクトを追加"))
                    {
                        const Result result = m_bridge->submit_command(
                            std::make_unique<AddObjectCommand>(
                                AddObjectType::Sprite2D));
                        if (!result)
                        {
                            log_result("Failed to add 2D object", result);
                            set_status_message(
                                "2D オブジェクトの追加に失敗しました。", true);
                        }
                        else
                        {
                            set_status_message(
                                "2D オブジェクトを追加しました。", false);
                        }
                    }

                    ImGui::EndMenu();
                }

                draw_main_camera_menu();

                ImGui::Separator();

                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                {
                    undo_last_command();
                }

                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                {
                    redo_last_command();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("実行"))
            {
                const bool isPlaying =
                    m_engine != nullptr && m_engine->is_playing();
                const bool canStartPlay =
                    m_engine != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive && !isPlaying;
                const bool canStopPlay =
                    m_engine != nullptr && !m_isScriptActionActive && isPlaying;

                if (ImGui::MenuItem("Play", nullptr, false, canStartPlay))
                {
                    const Result result = start_play_mode();
                    if (!result)
                    {
                        log_result("Failed to start play mode", result);
                        set_status_message("Play 開始に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を開始しました。", false);
                    }
                }

                if (ImGui::MenuItem("Stop", nullptr, false, canStopPlay))
                {
                    const Result result = stop_play_mode();
                    if (!result)
                    {
                        log_result("Failed to stop play mode", result);
                        set_status_message("Play 停止に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を停止しました。", false);
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit", nullptr, false, canStopPlay))
                {
                    const Result result = exit_play_mode();
                    if (!result)
                    {
                        log_result("Failed to exit play mode", result);
                        set_status_message("Play 終了に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を終了して editor に戻りました。", false);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("ビルド"))
            {
                const bool canEditBuildSettings = !m_isScriptActionActive;
                const bool canBuildScript =
                    m_buildSystem != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;
                const bool canBuildGameRelease =
                    m_buildSystem != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;
                const bool canReloadScript =
                    m_engine != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;

                if (ImGui::BeginMenu("GameScript ビルド構成", canEditBuildSettings))
                {
                    const auto draw_configuration_item =
                        [this](const char* a_label, BuildConfiguration a_configuration)
                    {
                        const bool isSelected =
                            m_scriptBuildConfiguration == a_configuration;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_script_build_configuration(a_configuration);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save GameScript build configuration",
                                    result);
                                set_status_message(
                                    "GameScript のビルド構成保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("GameScript のビルド構成を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_configuration_item("Debug", BuildConfiguration::Debug);
                    draw_configuration_item("RelWithDebInfo",
                        BuildConfiguration::RelWithDebInfo);
                    draw_configuration_item("Release",
                        BuildConfiguration::Release);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("GameScript 読み込み構成", canEditBuildSettings))
                {
                    const auto draw_configuration_item =
                        [this](const char* a_label, BuildConfiguration a_configuration)
                    {
                        const bool isSelected =
                            m_scriptLoadConfiguration == a_configuration;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_script_load_configuration(a_configuration);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save GameScript load configuration",
                                    result);
                                set_status_message(
                                    "GameScript の読み込み構成保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("GameScript の読み込み構成を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_configuration_item("Debug", BuildConfiguration::Debug);
                    draw_configuration_item("RelWithDebInfo",
                        BuildConfiguration::RelWithDebInfo);
                    draw_configuration_item("Release",
                        BuildConfiguration::Release);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("GameScript backend", canEditBuildSettings))
                {
                    const auto draw_backend_item =
                        [this](const char* a_label, BuildBackend a_backend)
                    {
                        const bool isSelected =
                            m_scriptBuildBackend == a_backend;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_script_build_backend(a_backend);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save GameScript build backend",
                                    result);
                                set_status_message(
                                    "GameScript の build backend 保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("GameScript backend を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_backend_item("CMake", BuildBackend::CMake);
                    draw_backend_item("VisualStudio", BuildBackend::VisualStudio);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("ゲーム配布ビルド構成", canEditBuildSettings))
                {
                    const auto draw_configuration_item =
                        [this](const char* a_label, BuildConfiguration a_configuration)
                    {
                        const bool isSelected =
                            m_gameReleaseBuildConfiguration == a_configuration;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_game_release_build_configuration(a_configuration);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save game release build configuration",
                                    result);
                                set_status_message(
                                    "ゲーム配布ビルド構成の保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("ゲーム配布ビルド構成を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_configuration_item("Debug", BuildConfiguration::Debug);
                    draw_configuration_item("RelWithDebInfo",
                        BuildConfiguration::RelWithDebInfo);
                    draw_configuration_item("Release",
                        BuildConfiguration::Release);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("ゲーム配布 backend", canEditBuildSettings))
                {
                    const auto draw_backend_item =
                        [this](const char* a_label, BuildBackend a_backend)
                    {
                        const bool isSelected =
                            m_gameReleaseBuildBackend == a_backend;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_game_release_build_backend(a_backend);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save game release build backend",
                                    result);
                                set_status_message(
                                    "ゲーム配布 backend の保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("ゲーム配布 backend を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_backend_item("CMake", BuildBackend::CMake);
                    draw_backend_item("VisualStudio", BuildBackend::VisualStudio);

                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem(
                        "GameScript を再読み込み", nullptr, false, canReloadScript))
                {
                    queue_script_action(PendingScriptAction::Reload);
                }

                if (ImGui::MenuItem(
                        "GameScript をビルド", nullptr, false, canBuildScript))
                {
                    queue_script_action(PendingScriptAction::Build);
                }

                if (ImGui::MenuItem(
                        "ゲーム Release ビルド", nullptr, false, canBuildGameRelease))
                {
                    set_status_message("ゲーム Release ビルドを開始しています...", false);
                    const Result result = build_game_release();
                    if (!result)
                    {
                        const std::string detail =
                            make_primary_build_message(m_lastGameReleaseBuildResult);
                        log_result("Failed to build game release", result);
                        set_status_message(
                            detail.empty()
                            ? "ゲーム Release ビルドに失敗しました。"
                            : "ゲーム Release ビルドに失敗しました: " + detail,
                            true);
                        set_script_build_notification(
                            "Game Release Build Failed",
                            detail.empty() ? std::string(result.message) : detail,
                            true,
                            true);
                    }
                    else
                    {
                        const std::string detail =
                            make_primary_build_message(m_lastGameReleaseBuildResult);
                        set_status_message(
                            detail.empty()
                            ? "ゲーム Release ビルドが成功しました。"
                            : "ゲーム Release ビルドが成功しました: " + detail,
                            false);
                        set_script_build_notification(
                            "Game Release Build Succeeded",
                            detail.empty()
                            ? "ゲーム Release ビルドに成功しました。"
                            : detail,
                            false,
                            false);
                    }
                }

                if (ImGui::MenuItem(
                        "ゲーム Release ビルドフォルダを開く", nullptr, false,
                        canBuildGameRelease))
                {
                    const Result result = open_game_release_build_directory();
                    if (!result)
                    {
                        log_result("Failed to open game release build directory", result);
                        set_status_message(
                            "ゲーム Release ビルドフォルダを開けませんでした。", true);
                    }
                    else
                    {
                        set_status_message(
                            "ゲーム Release ビルドフォルダを開きました。", false);
                    }
                }

                if (ImGui::MenuItem(
                        "GameScript を追加", nullptr, false,
                        !m_projectPath.empty() && !m_isScriptActionActive))
                {
                    m_createScriptNameBuffer.fill('\0');
                    m_openCreateScriptPopup = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem(
                        "GameScript solution を開く", nullptr, false,
                        m_visualStudioBridge != nullptr && !m_projectPath.empty()))
                {
                    const Result result = open_script_solution_in_visual_studio();
                    if (!result)
                    {
                        log_result("Failed to open GameScript solution", result);
                        set_status_message(
                            "GameScript solution を開けませんでした。", true);
                    }
                    else
                    {
                        set_status_message(
                            "GameScript solution を Visual Studio で開きました。",
                            false);
                    }
                }

                if (ImGui::MenuItem(
                        "Editor にデバッガをアタッチ", nullptr, false,
                        m_visualStudioBridge != nullptr && !m_projectPath.empty()))
                {
                    const Result result =
                        attach_editor_debugger_in_visual_studio();
                    if (!result)
                    {
                        log_result("Failed to attach debugger", result);
                        set_status_message(
                            "Visual Studio から Editor にアタッチできませんでした。",
                            true);
                    }
                    else
                    {
                        set_status_message(
                            "Visual Studio から Editor にアタッチしました。",
                            false);
                    }
                }

                ImGui::Separator();
                ImGui::MenuItem(
                    "Script Build Output",
                    nullptr,
                    &m_showScriptBuildOutput,
                    true);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Test"))
            {
                if (ImGui::MenuItem("Show Metrics Window"))
                {
                    showMetricsWindow = !showMetricsWindow;
                }

                if (ImGui::MenuItem("Show Demo Window"))
                {
                    showDemoWindow = !showDemoWindow;
                }

                if (ImGui::MenuItem("Show Style Editor"))
                {
                    showStyleEditor = !showStyleEditor;
                }

                ImGui::EndMenu();
            }

            if (!m_statusMessage.empty())
            {
                ImGui::Separator();
                if (m_hasStatusError)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
                }
                ImGui::TextUnformatted(m_statusMessage.c_str());
                if (m_hasStatusError)
                {
                    ImGui::PopStyleColor();
                }
            }

            if (m_hasScriptBuildNotification &&
                !m_scriptBuildNotificationTitle.empty())
            {
                ImGui::Separator();
                if (m_hasScriptBuildNotificationError)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, IM_COL32(96, 220, 120, 255));
                }

                ImGui::TextUnformatted(
                    m_scriptBuildNotificationTitle.c_str());
                ImGui::PopStyleColor();

                if (!m_scriptBuildNotificationMessage.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(
                        m_scriptBuildNotificationMessage.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Build Output"))
                {
                    m_showScriptBuildOutput = true;
                }
            }

            ImGui::EndMenuBar();
        }
        menuBarTimer.stop();
        m_currentUpdateMetrics.menuBarMs =
            menuBarTimer.elapsed_ticks().ms_f64();

        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();

        Core::Time::Timer optionalWindowsTimer(m_platform->clock());
        optionalWindowsTimer.start();
        if (showMetricsWindow)
        {
            ImGui::ShowMetricsWindow(&showMetricsWindow);
        }
        if (showDemoWindow)
        {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }
        if (showStyleEditor)
        {
            ImGui::ShowStyleEditor();
        }
        optionalWindowsTimer.stop();
        m_currentUpdateMetrics.optionalWindowsMs =
            optionalWindowsTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer statisticsTimer(m_platform->clock());
        statisticsTimer.start();
        m_statistics->update();
        statisticsTimer.stop();
        m_currentUpdateMetrics.statisticsMs =
            statisticsTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer gameViewTimer(m_platform->clock());
        gameViewTimer.start();
        m_gameView->update();
        gameViewTimer.stop();
        m_currentUpdateMetrics.gameViewMs =
            gameViewTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer debugViewTimer(m_platform->clock());
        debugViewTimer.start();
        m_debugView->update();
        debugViewTimer.stop();
        m_currentUpdateMetrics.debugViewMs =
            debugViewTimer.elapsed_ticks().ms_f64();
        process_debug_pick_request();
        sync_debug_selection();
        if (m_engine != nullptr)
        {
            m_engine->set_debug_view_camera(m_debugCamera.view_projection());
        }
        if (m_assetBrowser != nullptr)
        {
            Core::Time::Timer assetBrowserTimer(m_platform->clock());
            assetBrowserTimer.start();
            m_assetBrowser->update();
            assetBrowserTimer.stop();
            m_currentUpdateMetrics.assetBrowserMs =
                assetBrowserTimer.elapsed_ticks().ms_f64();
        }

        Core::Time::Timer createScriptPopupTimer(m_platform->clock());
        createScriptPopupTimer.start();
        draw_create_script_popup();
        createScriptPopupTimer.stop();
        m_currentUpdateMetrics.createScriptPopupMs =
            createScriptPopupTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer scriptBuildNotificationTimer(m_platform->clock());
        scriptBuildNotificationTimer.start();
        draw_script_build_notification_popup();
        scriptBuildNotificationTimer.stop();
        m_currentUpdateMetrics.scriptBuildNotificationMs =
            scriptBuildNotificationTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer scriptBuildOutputTimer(m_platform->clock());
        scriptBuildOutputTimer.start();
        draw_script_build_output();
        scriptBuildOutputTimer.stop();
        m_currentUpdateMetrics.scriptBuildOutputMs =
            scriptBuildOutputTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer hierarchyTimer(m_platform->clock());
        hierarchyTimer.start();
        m_hierarchy->update();
        hierarchyTimer.stop();
        m_currentUpdateMetrics.hierarchyMs =
            hierarchyTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer inspectorTimer(m_platform->clock());
        inspectorTimer.start();
        m_inspector->update();
        inspectorTimer.stop();
        m_currentUpdateMetrics.inspectorMs =
            inspectorTimer.elapsed_ticks().ms_f64();

        updateTimer.stop();
        m_currentUpdateMetrics.totalMs =
            updateTimer.elapsed_ticks().ms_f64();
        m_lastUpdateMetrics = m_currentUpdateMetrics;
    }
}
