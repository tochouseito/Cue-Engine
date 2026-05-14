#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>
#include <IO/Path.h>
#include <Threading/JobSystem.h>
#include <Time/Timer.h>

// === Engine includes ===
#include <ModelImporter.h>
#include <ModelCooker.h>
#include <SoundCooker.h>
#include <TextureCooker.h>
#include <GameCore/Navigation/Navigation.h>
#include <GameCore/SceneSerializer.h>
#include <Script/MarionnetteObject.h>
#include <ShadowSystem/GpuData/ShadowData.h>

// === Win includes ===
#include <shellapi.h>

// === C++ includes ===
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

// === ThirdParty includes ===
#include <ImGuizmo.h>
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
            BuildConfiguration gameReleaseBuildConfiguration =
                BuildConfiguration::Release;
            BuildBackend gameReleaseBuildBackend = BuildBackend::CMake;
            std::string gameReleaseOutputRoot = "Builds/Windows";
            std::string gameReleaseExecutableName = "Game";
            std::string gameReleaseWindowTitle = "Cue App";
            std::string gameReleaseIconPath{};
        };

        struct PreparedSceneReloadAssets final
        {
            std::vector<Core::IO::Path> texturePaths{};
            std::vector<Core::IO::Path> modelPaths{};
            std::vector<Core::IO::Path> materialPaths{};
        };

        struct SceneCameraMenuEntry final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            std::string name{};
            bool isMain = false;
        };

        struct RayDistance final
        {
            float distanceSq = 0.0f;
            float rayDistance = 0.0f;
        };

        constexpr float k_cameraFrustumNear = 0.03f;
        constexpr float k_cameraFrustumFar = 1.0f;
        constexpr uint32_t k_autoScriptBuildScanIntervalFrames = 30;
        constexpr uint32_t k_autoScriptBuildDebounceFrames = 45;

        [[nodiscard]] bool transform_nearly_equal(
            const ECS::TransformComponent& a_left,
            const ECS::TransformComponent& a_right) noexcept
        {
            auto isClose =
                [](float a_leftValue, float a_rightValue) noexcept
            {
                constexpr float k_epsilon = 0.0001f;
                return std::abs(a_leftValue - a_rightValue) <= k_epsilon;
            };
            auto isClose3 =
                [&isClose](
                    const Math::float3& a_leftValue,
                    const Math::float3& a_rightValue) noexcept
            {
                return isClose(a_leftValue.x, a_rightValue.x) &&
                    isClose(a_leftValue.y, a_rightValue.y) &&
                    isClose(a_leftValue.z, a_rightValue.z);
            };

            return isClose3(a_left.position, a_right.position) &&
                isClose3(a_left.rotation, a_right.rotation) &&
                isClose3(a_left.scale, a_right.scale);
        }

        void draw_gizmo_mode_button(
            const char* a_label,
            uint32_t a_value,
            uint32_t& a_inOutValue) noexcept
        {
            const bool isSelected = a_inOutValue == a_value;
            if (isSelected)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }

            if (ImGui::Button(a_label, ImVec2(44.0f, 0.0f)))
            {
                a_inOutValue = a_value;
            }

            if (isSelected)
            {
                ImGui::PopStyleColor();
            }
        }

        template<size_t BufferSize>
        void set_text_buffer(
            std::array<char, BufferSize>& a_buffer,
            std::string_view a_text) noexcept
        {
            a_buffer.fill('\0');
            const size_t copySize =
                (std::min)(a_text.size(), a_buffer.size() - 1);
            std::copy_n(a_text.data(), copySize, a_buffer.data());
        }

        [[nodiscard]] std::string to_lower_ascii(std::string a_text)
        {
            std::transform(
                a_text.begin(),
                a_text.end(),
                a_text.begin(),
                [](unsigned char a_char)
                {
                    return static_cast<char>(std::tolower(a_char));
                });
            return a_text;
        }

        [[nodiscard]] bool is_ignored_script_directory(
            const std::filesystem::path& a_path)
        {
            const std::string name = to_lower_ascii(a_path.filename().string());
            return name == ".git" || name == ".vs" || name == ".vscode" ||
                name == "build" || name == "builds" || name == "generated" ||
                name == "out" || name == "bin" || name == "obj" ||
                name == "cmakefiles" || name == "x64";
        }

        [[nodiscard]] bool is_watched_script_file(
            const std::filesystem::path& a_path)
        {
            const std::string filename = a_path.filename().string();
            if (filename == "CMakeLists.txt" || filename == "CMakePresets.json")
            {
                return true;
            }

            const std::string extension =
                to_lower_ascii(a_path.extension().string());
            return extension == ".c" || extension == ".cc" ||
                extension == ".cpp" || extension == ".cxx" ||
                extension == ".h" || extension == ".hh" ||
                extension == ".hpp" || extension == ".hxx" ||
                extension == ".inl" || extension == ".ixx" ||
                extension == ".cmake";
        }

        [[nodiscard]] bool is_supported_external_asset_file(
            const Core::IO::Path& a_path)
        {
            const std::string extension =
                to_lower_ascii(a_path.extension());
            return extension == ".png" || extension == ".dds" ||
                extension == ".wav" ||
                extension == ".obj";
        }

        [[nodiscard]] bool is_source_texture_file(
            const Core::IO::Path& a_path)
        {
            const std::string extension =
                to_lower_ascii(a_path.extension());
            return extension == ".png" || extension == ".dds";
        }

        [[nodiscard]] Result read_cuetexture_header(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_path,
            CueTextureHeader& outHeader)
        {
            if (to_lower_ascii(a_path.extension()) != ".cuetexture")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture asset file extension must be .cuetexture.");
            }

            std::vector<std::byte> fileData{};
            const Result result = a_fileSystem.read_all(a_path.normalize(), &fileData);
            if (!result || fileData.size() < sizeof(CueTextureHeader))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked texture header is invalid.");
            }

            std::memcpy(&outHeader, fileData.data(), sizeof(CueTextureHeader));
            if (outHeader.magic != k_cueTextureMagic ||
                outHeader.version != k_cueTextureVersion ||
                outHeader.width == 0 ||
                outHeader.height == 0 ||
                outHeader.mipCount == 0)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked texture header is invalid.");
            }

            return Result::ok();
        }

        [[nodiscard]] bool is_cube_cuetexture_file(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_path)
        {
            CueTextureHeader header{};
            const Result result =
                read_cuetexture_header(a_fileSystem, a_path, header);
            return result &&
                header.arraySize == 6 &&
                (header.flags & k_cueTextureFlagCubeMap) != 0;
        }

        [[nodiscard]] std::string trim_ascii(std::string a_text)
        {
            const auto isSpace =
                [](unsigned char a_char)
            {
                return std::isspace(a_char) != 0;
            };

            a_text.erase(a_text.begin(), std::find_if(
                a_text.begin(),
                a_text.end(),
                [isSpace](char a_char)
                {
                    return !isSpace(static_cast<unsigned char>(a_char));
                }));
            a_text.erase(std::find_if(
                a_text.rbegin(),
                a_text.rend(),
                [isSpace](char a_char)
                {
                    return !isSpace(static_cast<unsigned char>(a_char));
                }).base(), a_text.end());
            return a_text;
        }

        [[nodiscard]] std::string normalize_executable_stem(
            std::string a_name)
        {
            a_name = trim_ascii(std::move(a_name));
            if (to_lower_ascii(Core::IO::Path(a_name).extension()) == ".exe")
            {
                a_name = Core::IO::Path(a_name).stem();
            }
            return a_name.empty() ? std::string("Game") : a_name;
        }

        [[nodiscard]] bool is_valid_executable_stem(
            std::string_view a_name) noexcept
        {
            if (a_name.empty() || a_name == "." || a_name == "..")
            {
                return false;
            }

            for (const char c : a_name)
            {
                switch (c)
                {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return false;
                default:
                    break;
                }
            }

            return true;
        }

        void finish_background_operation(
            EditorManager::SceneReloadOperation& a_operation,
            const Result& a_result) noexcept;

        void set_background_progress(
            EditorManager::SceneReloadOperation& a_operation,
            std::string a_detail,
            uint32_t a_completed,
            uint32_t a_total) noexcept;

        void advance_background_progress(
            EditorManager::SceneReloadOperation& a_operation,
            std::string a_detail) noexcept;

        [[nodiscard]] Result load_project_settings(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectPath,
            ProjectSettings& a_outSettings) noexcept;

        void hash_bytes(
            uint64_t& a_inOutHash,
            const void* a_data,
            size_t a_size) noexcept
        {
            constexpr uint64_t k_fnvPrime = 1099511628211ull;
            const auto* bytes = static_cast<const unsigned char*>(a_data);
            for (size_t byteIndex = 0; byteIndex < a_size; ++byteIndex)
            {
                a_inOutHash ^= static_cast<uint64_t>(bytes[byteIndex]);
                a_inOutHash *= k_fnvPrime;
            }
        }

        [[nodiscard]] Math::float3 transform_point(
            const Math::float4x4& a_matrix,
            const Math::float3& a_point) noexcept
        {
            return Math::float3(
                a_point.x * a_matrix.values[0][0] +
                    a_point.y * a_matrix.values[1][0] +
                    a_point.z * a_matrix.values[2][0] +
                    a_matrix.values[3][0],
                a_point.x * a_matrix.values[0][1] +
                    a_point.y * a_matrix.values[1][1] +
                    a_point.z * a_matrix.values[2][1] +
                    a_matrix.values[3][1],
                a_point.x * a_matrix.values[0][2] +
                    a_point.y * a_matrix.values[1][2] +
                    a_point.z * a_matrix.values[2][2] +
                    a_matrix.values[3][2]);
        }

        [[nodiscard]] Math::float3 transform_direction(
            const Math::float3& a_direction,
            const Math::float3& a_rotation) noexcept
        {
            const Math::float4x4 rotationMatrix =
                Math::xyz_rotate_matrix(a_rotation);
            Math::float3 direction(
                a_direction.x * rotationMatrix.values[0][0] +
                    a_direction.y * rotationMatrix.values[1][0] +
                    a_direction.z * rotationMatrix.values[2][0],
                a_direction.x * rotationMatrix.values[0][1] +
                    a_direction.y * rotationMatrix.values[1][1] +
                    a_direction.z * rotationMatrix.values[2][1],
                a_direction.x * rotationMatrix.values[0][2] +
                    a_direction.y * rotationMatrix.values[1][2] +
                    a_direction.z * rotationMatrix.values[2][2]);
            direction.normalize();
            return direction;
        }

        [[nodiscard]] Math::float3 light_forward_axis(
            const ECS::TransformComponent& a_transform) noexcept
        {
            return transform_direction(
                Math::float3(0.0f, 0.0f, -1.0f),
                a_transform.rotation);
        }

        [[nodiscard]] Math::float3 make_camera_frustum_corner(
            uint32_t a_cornerIndex,
            const ECS::CameraComponent& a_camera,
            float a_distance) noexcept
        {
            const uint32_t planeCornerIndex = a_cornerIndex % 4u;
            const float fovY = std::clamp(a_camera.fovY, 1.0f, 179.0f);
            const float aspectRatio =
                a_camera.aspectRatio > 0.0f ? a_camera.aspectRatio : 1.0f;
            const float halfHeight =
                a_distance * std::tan(fovY * Math::k_pi / 180.0f * 0.5f);
            const float halfWidth = halfHeight * aspectRatio;
            const float x =
                (planeCornerIndex == 1u || planeCornerIndex == 2u)
                ? halfWidth
                : -halfWidth;
            const float y = planeCornerIndex >= 2u ? halfHeight : -halfHeight;
            return Math::float3(x, y, a_distance);
        }

        [[nodiscard]] bool distance_ray_segment(
            const DebugCamera::Ray& a_ray,
            const Math::float3& a_start,
            const Math::float3& a_end,
            RayDistance& a_outDistance) noexcept
        {
            constexpr float k_epsilon = 0.000001f;
            const Math::float3 segment = a_end - a_start;
            const float segmentLengthSq = segment.length_sq();
            if (segmentLengthSq <= k_epsilon)
            {
                return false;
            }

            const Math::float3 rayToStart = a_ray.origin - a_start;
            const float raySegmentDot = a_ray.direction.dot(segment);
            const float rayStartDot = a_ray.direction.dot(rayToStart);
            const float segmentStartDot = segment.dot(rayToStart);
            const float denominator = segmentLengthSq -
                raySegmentDot * raySegmentDot;

            float rayDistance = 0.0f;
            float segmentDistance = 0.0f;
            if (std::abs(denominator) > k_epsilon)
            {
                rayDistance =
                    (raySegmentDot * segmentStartDot -
                        segmentLengthSq * rayStartDot) /
                    denominator;
                segmentDistance =
                    (segmentStartDot - raySegmentDot * rayStartDot) /
                    denominator;
            }

            if (rayDistance < 0.0f)
            {
                rayDistance = 0.0f;
                segmentDistance =
                    std::clamp(segmentStartDot / segmentLengthSq, 0.0f, 1.0f);
            }
            else if (segmentDistance < 0.0f)
            {
                segmentDistance = 0.0f;
                rayDistance = (std::max)(-rayStartDot, 0.0f);
            }
            else if (segmentDistance > 1.0f)
            {
                segmentDistance = 1.0f;
                rayDistance =
                    (std::max)(raySegmentDot - rayStartDot, 0.0f);
            }

            const Math::float3 rayPoint =
                a_ray.origin + a_ray.direction * rayDistance;
            const Math::float3 segmentPoint =
                a_start + segment * segmentDistance;
            a_outDistance.distanceSq = (rayPoint - segmentPoint).length_sq();
            a_outDistance.rayDistance = rayDistance;
            return true;
        }

        [[nodiscard]] bool distance_ray_point(
            const DebugCamera::Ray& a_ray,
            const Math::float3& a_point,
            RayDistance& a_outDistance) noexcept
        {
            const Math::float3 rayToPoint = a_point - a_ray.origin;
            const float rayDistance = rayToPoint.dot(a_ray.direction);
            if (rayDistance < 0.0f)
            {
                return false;
            }

            const Math::float3 rayPoint =
                a_ray.origin + a_ray.direction * rayDistance;
            a_outDistance.distanceSq = (a_point - rayPoint).length_sq();
            a_outDistance.rayDistance = rayDistance;
            return true;
        }

        [[nodiscard]] float debug_pick_radius(float a_rayDistance) noexcept
        {
            return (std::max)(0.08f, a_rayDistance * 0.01f);
        }

        [[nodiscard]] bool should_close_menu_on_hover_leave(
            bool a_isMenuLabelHovered) noexcept
        {
            constexpr ImGuiHoveredFlags k_hoveredFlags =
                ImGuiHoveredFlags_ChildWindows |
                ImGuiHoveredFlags_AllowWhenBlockedByPopup;
            return !a_isMenuLabelHovered && !ImGui::IsWindowHovered(k_hoveredFlags);
        }

        [[nodiscard]] Core::IO::Path resolve_asset_root(
            const Core::IO::Path& a_projectRoot,
            const ProjectSettings& a_settings) noexcept
        {
            Core::IO::Path assetRoot(a_settings.assetRoot);
            if (!assetRoot.is_absolute())
            {
                assetRoot = Core::IO::Path::join(a_projectRoot, assetRoot);
            }
            return assetRoot.normalize();
        }

        [[nodiscard]] GameCore::NavMeshBakeSettings
            make_default_nav_mesh_settings() noexcept
        {
            GameCore::NavMeshBakeSettings settings{};
            settings.agentRadius = 0.35f;
            settings.agentHeight = 1.8f;
            settings.agentMaxClimb = 0.4f;
            settings.regionMinSize = 2.0f;
            settings.regionMergeSize = 8.0f;
            return settings;
        }

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

        [[nodiscard]] Result ensure_project_model_loaded(
            Engine& a_engine,
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_assetRoot,
            std::string_view a_modelName) noexcept
        {
            if (a_modelName.empty())
            {
                return Result::ok();
            }

            ModelHandle existingHandle{};
            if (a_engine.asset_manager().get_model(
                    a_modelName, existingHandle))
            {
                return Result::ok();
            }

            const Core::IO::Path modelRoot = Core::IO::Path::join(
                a_assetRoot, Core::IO::Path("Models"));
            const std::string modelFileName =
                std::string(a_modelName) + ".obj";
            const std::string cookedFileName =
                std::string(a_modelName) + ".cuemodel";
            const Core::IO::Path sourceModelPath = Core::IO::Path::join(
                modelRoot, Core::IO::Path(modelFileName));
            const Core::IO::Path cookedModelPath = Core::IO::Path::join(
                modelRoot, Core::IO::Path(cookedFileName));

            bool hasSourceModel = false;
            Result result =
                a_fileSystem.exists(sourceModelPath, &hasSourceModel);
            if (!result)
            {
                return result;
            }
            if (hasSourceModel)
            {
                result = ModelCooker::ensure_cuemodel_is_up_to_date(
                    a_fileSystem, sourceModelPath, cookedModelPath);
                if (!result)
                {
                    return result;
                }
            }

            bool hasCookedModel = false;
            result = a_fileSystem.exists(cookedModelPath, &hasCookedModel);
            if (!result)
            {
                return result;
            }
            if (!hasCookedModel)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "Navigation bake model asset was not found.");
            }

            ModelHandle loadedHandle{};
            return a_engine.asset_manager().register_model_from_cuemodel(
                a_fileSystem,
                a_modelName,
                cookedModelPath,
                loadedHandle);
        }

        [[nodiscard]] Result ensure_scene_asset_models_loaded(
            Engine& a_engine,
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_assetRoot,
            GameCore::SceneAsset& a_sceneAsset) noexcept
        {
            std::unordered_set<std::string> requiredModelNames{};
            for (const GameCore::ObjectDefinition& object :
                 a_sceneAsset.objects())
            {
                if (const ECS::MeshFilterComponent* meshFilter =
                    object.prototype
                        .get_component_ptr<ECS::MeshFilterComponent>();
                    meshFilter != nullptr && !meshFilter->modelName.empty())
                {
                    requiredModelNames.insert(meshFilter->modelName);
                }

                if (const ECS::ColliderComponent* collider =
                    object.prototype
                        .get_component_ptr<ECS::ColliderComponent>();
                    collider != nullptr && !collider->meshModelName.empty())
                {
                    requiredModelNames.insert(collider->meshModelName);
                }
            }

            for (const std::string& modelName : requiredModelNames)
            {
                Result result = ensure_project_model_loaded(
                    a_engine, a_fileSystem, a_assetRoot, modelName);
                if (!result)
                {
                    return result;
                }
            }

            for (GameCore::ObjectDefinition& object : a_sceneAsset.objects())
            {
                ECS::MeshFilterComponent* meshFilter =
                    object.prototype
                        .get_component_ptr<ECS::MeshFilterComponent>();
                if (meshFilter == nullptr || meshFilter->modelName.empty())
                {
                    continue;
                }

                uint32_t meshId = ECS::k_invalidMeshId;
                Result result = a_engine.asset_manager().resolve_model_mesh_id(
                    meshFilter->modelName, meshId);
                if (!result)
                {
                    return result;
                }

                meshFilter->meshId = meshId;
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
                else if (is_source_texture_file(texturePath))
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

        [[nodiscard]] Result list_directory_if_exists(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_directory,
            std::vector<Core::IO::Path>& a_outEntries) noexcept
        {
            a_outEntries.clear();

            bool exists = false;
            Result result = a_fileSystem.exists(a_directory, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::ok();
            }

            result = a_fileSystem.list_directory(a_directory, &a_outEntries);
            if (!result)
            {
                return result;
            }

            std::sort(a_outEntries.begin(), a_outEntries.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.utf8() < a_right.utf8();
                });
            return Result::ok();
        }

        void unique_normalized_paths(
            std::vector<Core::IO::Path>& a_paths)
        {
            std::sort(a_paths.begin(), a_paths.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.normalize().utf8() <
                        a_right.normalize().utf8();
                });
            a_paths.erase(
                std::unique(a_paths.begin(), a_paths.end(),
                    [](const Core::IO::Path& a_left,
                        const Core::IO::Path& a_right)
                    {
                        return a_left.normalize().utf8() ==
                            a_right.normalize().utf8();
                    }),
                a_paths.end());
        }

        [[nodiscard]] Result prepare_scene_reload_assets(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectRoot,
            EditorManager::SceneReloadOperation& a_operation) noexcept
        {
            set_background_progress(a_operation, "プロジェクト設定を読み込み中...", 0, 1);

            ProjectSettings settings{};
            Result result = load_project_settings(
                a_fileSystem, a_projectRoot, settings);
            if (!result)
            {
                finish_background_operation(a_operation, result);
                return result;
            }

            const Core::IO::Path assetRoot =
                resolve_asset_root(a_projectRoot, settings);
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Textures"));
            const Core::IO::Path modelRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Models"));
            const Core::IO::Path materialRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Materials"));
            const Core::IO::Path soundRoot = Core::IO::Path::join(
                assetRoot, Core::IO::Path("Sounds"));

            std::vector<Core::IO::Path> textureEntries{};
            std::vector<Core::IO::Path> modelEntries{};
            std::vector<Core::IO::Path> materialEntries{};
            std::vector<Core::IO::Path> soundEntries{};

            result = list_directory_if_exists(
                a_fileSystem, textureRoot, textureEntries);
            if (!result)
            {
                finish_background_operation(a_operation, result);
                return result;
            }
            result = list_directory_if_exists(
                a_fileSystem, modelRoot, modelEntries);
            if (!result)
            {
                finish_background_operation(a_operation, result);
                return result;
            }
            result = list_directory_if_exists(
                a_fileSystem, materialRoot, materialEntries);
            if (!result)
            {
                finish_background_operation(a_operation, result);
                return result;
            }
            result = list_directory_if_exists(
                a_fileSystem, soundRoot, soundEntries);
            if (!result)
            {
                finish_background_operation(a_operation, result);
                return result;
            }

            std::vector<Core::IO::Path> sourceTexturePaths{};
            std::vector<Core::IO::Path> cookedTexturePaths{};
            for (const Core::IO::Path& texturePath : textureEntries)
            {
                if (is_source_texture_file(texturePath))
                {
                    sourceTexturePaths.push_back(texturePath);
                }
                else if (texturePath.extension() == ".cuetexture")
                {
                    cookedTexturePaths.push_back(texturePath);
                }
            }

            std::vector<Core::IO::Path> sourceModelPaths{};
            std::vector<Core::IO::Path> cookedModelPaths{};
            for (const Core::IO::Path& modelPath : modelEntries)
            {
                if (modelPath.extension() == ".obj")
                {
                    sourceModelPaths.push_back(modelPath);
                }
                else if (modelPath.extension() == ".cuemodel")
                {
                    cookedModelPaths.push_back(modelPath);
                }
            }

            std::vector<Core::IO::Path> materialPaths{};
            for (const Core::IO::Path& materialPath : materialEntries)
            {
                if (materialPath.extension() == ".cuematerial")
                {
                    materialPaths.push_back(materialPath);
                }
            }

            std::vector<Core::IO::Path> sourceSoundPaths{};
            for (const Core::IO::Path& soundPath : soundEntries)
            {
                if (soundPath.extension() == ".wav")
                {
                    sourceSoundPaths.push_back(soundPath);
                }
            }

            const uint32_t total =
                1u +
                static_cast<uint32_t>(sourceTexturePaths.size()) +
                static_cast<uint32_t>(sourceModelPaths.size()) +
                static_cast<uint32_t>(sourceSoundPaths.size()) +
                1u;
            set_background_progress(
                a_operation,
                "アセットを準備中...",
                1,
                total);

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
                    finish_background_operation(a_operation, result);
                    return result;
                }

                cookedTexturePaths.push_back(cookedTexturePath);
                advance_background_progress(
                    a_operation,
                    "Texture: " + sourceTexturePath.filename());
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
                    finish_background_operation(a_operation, result);
                    return result;
                }

                bool hasCookedModel = false;
                result = a_fileSystem.exists(cookedModelPath, &hasCookedModel);
                if (!result)
                {
                    finish_background_operation(a_operation, result);
                    return result;
                }
                if (hasCookedModel)
                {
                    cookedModelPaths.push_back(cookedModelPath);
                }

                advance_background_progress(
                    a_operation,
                    "Model: " + sourceModelPath.filename());
            }

            for (const Core::IO::Path& sourceSoundPath : sourceSoundPaths)
            {
                const Core::IO::Path cookedSoundPath = Core::IO::Path::join(
                    soundRoot,
                    Core::IO::Path(sourceSoundPath.stem() + ".cuesound"));
                result = SoundCooker::ensure_cuesound_is_up_to_date(
                    a_fileSystem,
                    sourceSoundPath,
                    cookedSoundPath);
                if (!result)
                {
                    finish_background_operation(a_operation, result);
                    return result;
                }

                advance_background_progress(
                    a_operation,
                    "Sound: " + sourceSoundPath.filename());
            }

            unique_normalized_paths(cookedTexturePaths);
            unique_normalized_paths(cookedModelPaths);
            unique_normalized_paths(materialPaths);

            {
                std::lock_guard<std::mutex> lock(a_operation.mutex);
                a_operation.texturePaths = std::move(cookedTexturePaths);
                a_operation.modelPaths = std::move(cookedModelPaths);
                a_operation.materialPaths = std::move(materialPaths);
            }

            finish_background_operation(a_operation, Result::ok());
            return Result::ok();
        }

        [[nodiscard]] Result register_prepared_assets(
            Engine& a_engine,
            Core::IO::IFileSystem& a_fileSystem,
            std::span<const Core::IO::Path> a_texturePaths,
            std::span<const Core::IO::Path> a_modelPaths,
            std::span<const Core::IO::Path> a_materialPaths) noexcept
        {
            Result result = Result::ok();

            for (const Core::IO::Path& texturePath : a_texturePaths)
            {
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

            for (const Core::IO::Path& modelPath : a_modelPaths)
            {
                ModelHandle modelHandle{};
                result = a_engine.asset_manager().register_model_from_cuemodel(
                    a_fileSystem,
                    modelPath.stem(),
                    modelPath,
                    modelHandle);
                if (!result)
                {
                    return result;
                }
            }

            for (const Core::IO::Path& materialPath : a_materialPaths)
            {
                MaterialHandle materialHandle{};
                result = a_engine.asset_manager().load_material(
                    a_fileSystem,
                    materialPath,
                    materialHandle);
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
                "buildBackend が不正です。");
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
                root.erase("scriptLoadConfiguration");
                root.erase("scriptBuildBackend");
                root["gameReleaseBuildConfiguration"] =
                    BuildSystem::to_configuration_name(
                        a_settings.gameReleaseBuildConfiguration);
                root["gameReleaseBuildBackend"] =
                    to_build_backend_name(a_settings.gameReleaseBuildBackend);
                root["gameReleaseOutputRoot"] =
                    a_settings.gameReleaseOutputRoot;
                root["gameReleaseExecutableName"] =
                    normalize_executable_stem(
                        a_settings.gameReleaseExecutableName);
                root["gameReleaseWindowTitle"] =
                    a_settings.gameReleaseWindowTitle.empty()
                    ? std::string("Cue App")
                    : a_settings.gameReleaseWindowTitle;
                root["gameReleaseIconPath"] =
                    a_settings.gameReleaseIconPath;
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

                a_outSettings.gameReleaseExecutableName =
                    normalize_executable_stem(root.value(
                        "gameReleaseExecutableName",
                        std::string("Game")));
                if (!is_valid_executable_stem(
                        a_outSettings.gameReleaseExecutableName))
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "gameReleaseExecutableName が不正です。");
                }

                a_outSettings.gameReleaseWindowTitle =
                    root.value("gameReleaseWindowTitle",
                        root.value("name", std::string("Cue App")));
                if (a_outSettings.gameReleaseWindowTitle.empty())
                {
                    a_outSettings.gameReleaseWindowTitle = "Cue App";
                }

                a_outSettings.gameReleaseIconPath =
                    root.value("gameReleaseIconPath", std::string{});

                return Result::ok();
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "cueproject.json could not be parsed.");
            }
        }
    }

    namespace
    {
        void finish_background_operation(
            EditorManager::SceneReloadOperation& a_operation,
            const Result& a_result) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(a_operation.mutex);
                a_operation.resultCode = a_result.code;
                a_operation.resultSeverity = a_result.severity;
                a_operation.succeeded = static_cast<bool>(a_result);
                a_operation.errorMessage = std::string(a_result.message);
                if (a_operation.succeeded)
                {
                    a_operation.detail = "メインスレッドへ反映中...";
                    a_operation.completed.store(
                        a_operation.total.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
                }
            }

            a_operation.isFinished.store(true, std::memory_order_release);
        }

        void set_background_progress(
            EditorManager::SceneReloadOperation& a_operation,
            std::string a_detail,
            uint32_t a_completed,
            uint32_t a_total) noexcept
        {
            if (a_total == 0)
            {
                a_total = 1;
            }

            {
                std::lock_guard<std::mutex> lock(a_operation.mutex);
                a_operation.detail = std::move(a_detail);
            }

            a_operation.total.store(a_total, std::memory_order_relaxed);
            a_operation.completed.store(
                (std::min)(a_completed, a_total),
                std::memory_order_relaxed);
        }

        void advance_background_progress(
            EditorManager::SceneReloadOperation& a_operation,
            std::string a_detail) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(a_operation.mutex);
                a_operation.detail = std::move(a_detail);
            }

            const uint32_t total =
                (std::max)(a_operation.total.load(std::memory_order_relaxed), 1u);
            uint32_t completed =
                a_operation.completed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (completed > total)
            {
                a_operation.completed.store(total, std::memory_order_relaxed);
            }
        }
    }

    EditorManager::~EditorManager()
    {
        if (m_jobSystem != nullptr)
        {
            m_jobSystem->shutdown();
        }
    }

    void EditorManager::initialize()
    {
        m_debugGizmoOperation = static_cast<uint32_t>(ImGuizmo::TRANSLATE);
        m_debugGizmoMode = static_cast<uint32_t>(ImGuizmo::WORLD);

        if (m_fileSystem != nullptr)
        {
            m_buildSystem = std::make_unique<BuildSystem>(*m_fileSystem);
            if (m_platform != nullptr)
            {
                m_jobSystem = std::make_unique<Core::Threading::JobSystem>();
                const Result jobSystemResult =
                    m_jobSystem->initialize(m_platform->thread_factory(), 1, 2, 16);
                if (!jobSystemResult)
                {
                    log_result("Failed to initialize editor JobSystem",
                        jobSystemResult);
                    m_jobSystem.reset();
                }
            }
            m_visualStudioBridge =
                std::make_unique<VisualStudioBridge>(*m_fileSystem);
            m_assetBrowser = std::make_unique<AssetBrowser>(m_fileSystem);
            m_assetBrowser->set_selected_asset_path(&m_selectedAssetPath);
            m_assetBrowser->set_texture_preview_dependencies(
                &m_engine->asset_manager(),
                m_backend);
        }
        m_statistics =
            std::make_unique<Statistics>(m_engine->frame_controller(), *m_engine);
        m_statistics->set_update_metrics_source(&m_lastUpdateMetrics);
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend, &m_debugCamera);
        m_debugView->set_add_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_add_menu_items();
            });
        m_debugView->set_overlay_callback(
            this,
            [](void* a_context,
                const ImVec2& a_viewportMin,
                const ImVec2& a_viewportMax,
                ImDrawList* a_drawList)
            {
                return static_cast<EditorManager*>(a_context)
                    ->draw_debug_overlay(
                        a_viewportMin,
                        a_viewportMax,
                        a_drawList);
            });
        m_debugView->set_view_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_view_menu_items();
            });
        m_debugView->set_scene_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_scene_menu_items();
            });
        m_hierarchy = std::make_unique<Hierarchy>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId,
            &m_selectedSceneId);
        m_inspector = std::make_unique<Inspector>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId,
            &m_selectedAssetPath, m_engine, m_fileSystem);
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
        m_loadedEditorScenes.clear();
        m_selectedSceneId = GameCore::k_invalidSceneId;
        m_scriptBuildConfiguration = projectSettings.scriptBuildConfiguration;
        m_gameReleaseBuildConfiguration =
            projectSettings.gameReleaseBuildConfiguration;
        m_gameReleaseBuildBackend =
            projectSettings.gameReleaseBuildBackend;
        set_text_buffer(
            m_gameReleaseExecutableNameBuffer,
            projectSettings.gameReleaseExecutableName);
        set_text_buffer(
            m_gameReleaseWindowTitleBuffer,
            projectSettings.gameReleaseWindowTitle);
        set_text_buffer(
            m_gameReleaseIconPathBuffer,
            projectSettings.gameReleaseIconPath);

        Core::IO::Path assetRootPath(projectSettings.assetRoot);
        if (!assetRootPath.is_absolute())
        {
            assetRootPath = Core::IO::Path::join(
                Core::IO::Path(a_projectPath), assetRootPath);
        }
        m_assetRootPath = assetRootPath.normalize();
        m_selectedAssetPath = {};
        if (m_assetBrowser != nullptr)
        {
            m_assetBrowser->set_asset_root_path(m_assetRootPath);
        }
        if (m_inspector != nullptr)
        {
            m_inspector->set_asset_root_path(m_assetRootPath);
        }
        m_engine->set_asset_root_path(m_assetRootPath);

        const Result scriptLoadResult = m_engine->load_script_module(
            scriptRootPath,
            to_script_module_build_configuration(m_scriptBuildConfiguration));
        const bool canContinueWithoutScript =
            scriptLoadResult.code == Code::NotFound ||
            scriptLoadResult.code == Code::Unsupported;
        if (!scriptLoadResult && !canContinueWithoutScript)
        {
            set_status_message("GameScript.dll の読み込みに失敗しました。", true);
            return scriptLoadResult;
        }

        m_hasScriptSourceSnapshot = false;
        m_hasPendingAutoScriptBuild = false;
        m_autoScriptBuildScanDelayFrames = 0;
        m_autoScriptBuildDebounceFrames = 0;
        m_scriptSourceVersion = 0;

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
        else if (!scriptLoadResult && scriptLoadResult.code == Code::Unsupported)
        {
            set_status_message(
                "プロジェクトを開きました。GameScript.dll の ABI が古いため再ビルドが必要です。",
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
        sceneAsset.set_navigation_mesh_path(
            m_loadedSceneAsset.navigation_mesh_path());
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

        for (LoadedSceneEntry& entry : m_loadedEditorScenes)
        {
            if (entry.sceneId != m_currentSceneId || entry.asset == nullptr)
            {
                continue;
            }

            *entry.asset = sceneAsset;
            entry.name = sceneAsset.name().empty()
                ? Core::IO::Path(m_currentScenePath).stem()
                : sceneAsset.name();
            entry.path = m_currentScenePath;
            break;
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
            BuildBackend::CMake
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

        GameReleaseBuildResult configureResult{};
        result = m_buildSystem->execute_game_release_configure(
            projectRequest,
            configureResult);
        append_game_release_result(
            m_lastGameReleaseBuildResult,
            configureResult);
        for (const BuildStageResult& stageResult : configureResult.stageResults)
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
        const std::string executableName =
            normalize_executable_stem(
                projectSettings.gameReleaseExecutableName);
        const std::string executableFileName = executableName + ".exe";

        const Core::IO::Path projectCueAppPath = Core::IO::Path::join(
            projectOutputDirectory,
            Core::IO::Path(executableFileName));
        result = m_fileSystem->copy_file(
            projectCueAppPath,
            Core::IO::Path::join(
                stagingDirectory,
                Core::IO::Path(executableFileName)),
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

    Result EditorManager::create_material_asset()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Material 作成に必要な依存が初期化されていません。");
        }
        if (m_assetRootPath.is_empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Assets フォルダが設定されていません。");
        }

        const Core::IO::Path materialRoot = Core::IO::Path::join(
            m_assetRootPath, Core::IO::Path("Materials"));
        Result result = m_fileSystem->create_directories(materialRoot);
        if (!result)
        {
            return result;
        }

        for (uint32_t index = 0; index < 1000; ++index)
        {
            const std::string materialName =
                index == 0 ? std::string("Material")
                           : "Material" + std::to_string(index);
            const Core::IO::Path materialPath = Core::IO::Path::join(
                materialRoot, Core::IO::Path(materialName + ".cuematerial"));

            bool exists = false;
            result = m_fileSystem->exists(materialPath, &exists);
            if (!result)
            {
                return result;
            }

            MaterialHandle existingHandle{};
            if (exists ||
                m_engine->asset_manager().get_material(
                    materialName, existingHandle))
            {
                continue;
            }

            MaterialDesc materialDesc{};
            materialDesc.color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);

            MaterialHandle materialHandle{};
            result = m_engine->asset_manager().create_material(
                materialName, materialDesc, materialHandle);
            if (!result)
            {
                return result;
            }

            result = m_engine->asset_manager().save_material(
                materialHandle, *m_fileSystem, materialPath);
            if (!result)
            {
                return result;
            }

            m_selectedAssetPath = materialPath.normalize();
            m_selectedEntityId = GameCore::k_invalidEntityId;
            if (m_assetBrowser != nullptr)
            {
                m_assetBrowser->refresh();
            }
            return Result::ok();
        }

        return Result::fail(Code::CreateFailed, Severity::Error,
            "作成可能な Material 名が見つかりません。");
    }

    Result EditorManager::handle_dropped_asset_files()
    {
        if (m_platform == nullptr || m_fileSystem == nullptr)
        {
            return Result::ok();
        }

        std::vector<std::string> droppedFiles{};
        if (!m_platform->consume_dropped_files(droppedFiles) ||
            droppedFiles.empty())
        {
            return Result::ok();
        }

        const Core::IO::Path destinationDirectory =
            current_asset_drop_folder();
        if (destinationDirectory.is_empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Assets フォルダが設定されていません。");
        }

        uint32_t importedCount = 0;
        uint32_t skippedCount = 0;
        for (const std::string& droppedFile : droppedFiles)
        {
            const Core::IO::Path sourcePath(droppedFile);
            if (!is_supported_external_asset_file(sourcePath))
            {
                ++skippedCount;
                continue;
            }

            Core::IO::FileStat stat{};
            Result result = m_fileSystem->stat(sourcePath, &stat);
            if (!result)
            {
                return result;
            }
            if (stat.type != Core::IO::FileType::regular)
            {
                ++skippedCount;
                continue;
            }

            result = import_external_asset_file(
                sourcePath, destinationDirectory);
            if (!result)
            {
                return result;
            }
            ++importedCount;
        }

        if (importedCount > 0 && m_assetBrowser != nullptr)
        {
            m_assetBrowser->refresh();
        }

        if (importedCount == 0 && skippedCount > 0)
        {
            set_status_message(
                "対応していないファイルはインポートされませんでした。", true);
            return Result::ok();
        }

        if (importedCount > 0)
        {
            std::string message =
                std::to_string(importedCount) +
                " 件のアセットをインポートしました。";
            if (skippedCount > 0)
            {
                message += " 対応外ファイルはスキップしました。";
            }
            set_status_message(std::move(message), false);
        }

        return Result::ok();
    }

    Result EditorManager::import_external_asset_file(
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationDirectory)
    {
        Core::IO::Path destinationPath{};
        Result result = make_asset_import_destination(
            a_sourcePath, a_destinationDirectory, destinationPath);
        if (!result)
        {
            return result;
        }

        result = m_fileSystem->copy_file(
            a_sourcePath.normalize(), destinationPath, false);
        if (!result)
        {
            return result;
        }

        return import_copied_asset_file(destinationPath);
    }

    Result EditorManager::import_copied_asset_file(
        const Core::IO::Path& a_assetPath)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Asset import dependencies are not initialized.");
        }

        const std::string extension =
            to_lower_ascii(a_assetPath.extension());
        if (extension == ".png" || extension == ".dds")
        {
            const Core::IO::Path cookedPath = Core::IO::Path::join(
                a_assetPath.parent(),
                Core::IO::Path(a_assetPath.stem() + ".cuetexture"));
            Result result = TextureCooker::ensure_cuetexture_is_up_to_date(
                *m_fileSystem, a_assetPath, cookedPath);
            if (!result)
            {
                return result;
            }

            uint32_t textureId = AssetManager::k_errorTextureId;
            return m_engine->asset_manager().register_texture_from_cuetexture(
                *m_fileSystem,
                make_asset_relative_name(cookedPath),
                cookedPath,
                textureId);
        }

        if (extension == ".obj")
        {
            const Core::IO::Path cookedPath = Core::IO::Path::join(
                a_assetPath.parent(),
                Core::IO::Path(a_assetPath.stem() + ".cuemodel"));
            Result result = ModelCooker::ensure_cuemodel_is_up_to_date(
                *m_fileSystem, a_assetPath, cookedPath);
            if (!result)
            {
                return result;
            }

            ModelHandle modelHandle{};
            std::string modelName = cookedPath.stem();
            if (m_engine->asset_manager().get_model(modelName, modelHandle))
            {
                modelName = make_asset_relative_name(cookedPath);
            }
            return m_engine->asset_manager().register_model_from_cuemodel(
                *m_fileSystem,
                modelName,
                cookedPath,
                modelHandle);
        }

        if (extension == ".wav")
        {
            const Core::IO::Path cookedPath = Core::IO::Path::join(
                a_assetPath.parent(),
                Core::IO::Path(a_assetPath.stem() + ".cuesound"));
            return SoundCooker::ensure_cuesound_is_up_to_date(
                *m_fileSystem, a_assetPath, cookedPath);
        }

        return Result::ok();
    }

    Result EditorManager::make_asset_import_destination(
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationDirectory,
        Core::IO::Path& a_outDestinationPath) const
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "FileSystem が初期化されていません。");
        }

        Result result = m_fileSystem->create_directories(a_destinationDirectory);
        if (!result)
        {
            return result;
        }

        const std::string stem = a_sourcePath.stem();
        const std::string extension = to_lower_ascii(a_sourcePath.extension());
        for (uint32_t index = 0; index < 1000; ++index)
        {
            const std::string filename =
                index == 0 ? (stem + extension)
                           : (stem + std::to_string(index) + extension);
            const Core::IO::Path candidatePath = Core::IO::Path::join(
                a_destinationDirectory, Core::IO::Path(filename));

            bool exists = false;
            result = m_fileSystem->exists(candidatePath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                a_outDestinationPath = candidatePath;
                return Result::ok();
            }
        }

        return Result::fail(Code::CreateFailed, Severity::Error,
            "コピー先のファイル名を決定できませんでした。");
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
            BuildBackend::CMake
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
            to_script_module_build_configuration(m_scriptBuildConfiguration));
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

    Result EditorManager::save_game_release_app_settings(
        const std::string& a_executableName,
        const std::string& a_windowTitle,
        const std::string& a_iconPath)
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

        const std::string executableName =
            normalize_executable_stem(a_executableName);
        if (!is_valid_executable_stem(executableName))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "EXE 名に使用できない文字が含まれています。");
        }

        std::string windowTitle = trim_ascii(a_windowTitle);
        if (windowTitle.empty())
        {
            windowTitle = "Cue App";
        }

        std::string iconPath = trim_ascii(a_iconPath);
        const std::string iconExtension =
            to_lower_ascii(Core::IO::Path(iconPath).extension());
        const bool isSupportedIconPath =
            iconPath.empty() ||
            iconExtension == ".ico" ||
            iconExtension == ".png" ||
            iconExtension == ".jpg" ||
            iconExtension == ".jpeg" ||
            iconExtension == ".bmp";
        if (!isSupportedIconPath)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "アプリアイコンは .ico または画像ファイルを指定してください。");
        }
        if (!iconPath.empty())
        {
            Core::IO::Path resolvedIconPath(iconPath);
            if (!resolvedIconPath.is_absolute())
            {
                resolvedIconPath = Core::IO::Path::join(
                    Core::IO::Path(m_projectPath),
                    resolvedIconPath);
            }

            bool iconExists = false;
            Result result = m_fileSystem->exists(
                resolvedIconPath,
                &iconExists);
            if (!result)
            {
                return result;
            }
            if (!iconExists)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "アプリアイコンが見つかりません。");
            }
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.gameReleaseExecutableName = executableName;
        settings.gameReleaseWindowTitle = windowTitle;
        settings.gameReleaseIconPath = iconPath;

        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        set_text_buffer(m_gameReleaseExecutableNameBuffer, executableName);
        set_text_buffer(m_gameReleaseWindowTitleBuffer, windowTitle);
        set_text_buffer(m_gameReleaseIconPathBuffer, iconPath);
        return Result::ok();
    }

    Result EditorManager::load_game_release_app_settings_to_buffers()
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

        set_text_buffer(
            m_gameReleaseExecutableNameBuffer,
            settings.gameReleaseExecutableName);
        set_text_buffer(
            m_gameReleaseWindowTitleBuffer,
            settings.gameReleaseWindowTitle);
        set_text_buffer(
            m_gameReleaseIconPathBuffer,
            settings.gameReleaseIconPath);
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

    void EditorManager::update_auto_script_build()
    {
        if (m_projectPath.empty() || m_buildSystem == nullptr ||
            m_engine == nullptr)
        {
            m_hasScriptSourceSnapshot = false;
            m_hasPendingAutoScriptBuild = false;
            return;
        }

        if (m_autoScriptBuildScanDelayFrames > 0)
        {
            --m_autoScriptBuildScanDelayFrames;
        }
        else if (m_pendingScriptAction == PendingScriptAction::None &&
            !m_isScriptActionActive)
        {
            m_autoScriptBuildScanDelayFrames =
                k_autoScriptBuildScanIntervalFrames;

            uint64_t sourceVersion = 0;
            if (try_get_script_source_version(sourceVersion))
            {
                if (!m_hasScriptSourceSnapshot)
                {
                    m_scriptSourceVersion = sourceVersion;
                    m_hasScriptSourceSnapshot = true;
                }
                else if (sourceVersion != m_scriptSourceVersion)
                {
                    m_scriptSourceVersion = sourceVersion;
                    m_hasPendingAutoScriptBuild = true;
                    m_autoScriptBuildDebounceFrames =
                        k_autoScriptBuildDebounceFrames;
                }
            }
        }

        if (!m_hasPendingAutoScriptBuild)
        {
            return;
        }

        if (m_autoScriptBuildDebounceFrames > 0)
        {
            --m_autoScriptBuildDebounceFrames;
            return;
        }

        if (m_pendingScriptAction != PendingScriptAction::None ||
            m_isScriptActionActive || m_platform == nullptr ||
            !m_platform->is_window_focused())
        {
            return;
        }

        m_hasPendingAutoScriptBuild = false;
        queue_script_action(PendingScriptAction::Build);
        set_status_message(
            "GameScript の変更を検出したためビルドしています...", false);
    }

    bool EditorManager::try_get_script_source_version(
        uint64_t& a_outVersion) const
    {
        a_outVersion = 0;

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result || scriptRoot.is_empty())
        {
            return false;
        }

        const std::filesystem::path rootPath(scriptRoot.utf8());
        std::error_code error{};
        if (!std::filesystem::exists(rootPath, error) || error)
        {
            return false;
        }

        std::vector<std::pair<std::string, uint64_t>> sourceFiles{};
        constexpr auto options =
            std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator iterator(
            rootPath,
            options,
            error);
        if (error)
        {
            return false;
        }

        const std::filesystem::recursive_directory_iterator end{};
        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                error.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *iterator;
            if (entry.is_directory(error))
            {
                if (!error && is_ignored_script_directory(entry.path()))
                {
                    iterator.disable_recursion_pending();
                }
                error.clear();
                continue;
            }
            error.clear();

            if (!entry.is_regular_file(error))
            {
                error.clear();
                continue;
            }
            error.clear();

            if (!is_watched_script_file(entry.path()))
            {
                continue;
            }

            const std::filesystem::file_time_type writeTime =
                entry.last_write_time(error);
            if (error)
            {
                error.clear();
                continue;
            }

            std::filesystem::path relativePath =
                std::filesystem::relative(entry.path(), rootPath, error);
            if (error)
            {
                error.clear();
                relativePath = entry.path().filename();
            }

            sourceFiles.emplace_back(
                relativePath.generic_string(),
                static_cast<uint64_t>(writeTime.time_since_epoch().count()));
        }

        std::sort(
            sourceFiles.begin(),
            sourceFiles.end(),
            [](const auto& a_left, const auto& a_right)
            {
                return a_left.first < a_right.first;
            });

        uint64_t hash = 1469598103934665603ull;
        for (const auto& [relativePath, writeTime] : sourceFiles)
        {
            hash_bytes(hash, relativePath.data(), relativePath.size());
            hash_bytes(hash, &writeTime, sizeof(writeTime));
        }

        const uint64_t fileCount = static_cast<uint64_t>(sourceFiles.size());
        hash_bytes(hash, &fileCount, sizeof(fileCount));
        a_outVersion = hash;
        return true;
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

        result = load_scene_to_world(Core::IO::Path(m_currentScenePath), true);
        if (!result)
        {
            return result;
        }

        set_status_message("シーンを読み込みました。", false);
        return Result::ok();
    }

    Result EditorManager::start_background_scene_reload()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr || m_jobSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Background scene reload dependencies are not initialized.");
        }
        if (m_sceneReloadOperation != nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "Scene reload is already running.");
        }
        if (m_projectPath.empty() || m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no scene path to reload.");
        }

        auto operation = std::make_shared<SceneReloadOperation>();
        operation->title = "シーンを再読み込み中";
        operation->detail = "アセット準備ジョブを開始中...";

        Core::IO::IFileSystem* fileSystem = m_fileSystem;
        const Core::IO::Path projectRoot(m_projectPath);
        Result result = m_jobSystem->enqueue_job(
            "EditorSceneReloadPrepare",
            [fileSystem, projectRoot, operation]()
            {
                if (fileSystem == nullptr)
                {
                    finish_background_operation(
                        *operation,
                        Result::fail(Code::InvalidState, Severity::Error,
                            "FileSystem is not initialized."));
                    return;
                }

                (void)prepare_scene_reload_assets(
                    *fileSystem,
                    projectRoot,
                    *operation);
            },
            operation->future,
            Core::Threading::JobSystem::JobPriority::Normal);
        if (!result)
        {
            return result;
        }

        m_sceneReloadOperation = std::move(operation);
        m_isScriptActionActive = true;
        set_status_message("シーン再読み込みをバックグラウンドで開始しました。", false);
        return Result::ok();
    }

    void EditorManager::update_background_scene_reload()
    {
        if (m_sceneReloadOperation == nullptr)
        {
            return;
        }

        SceneReloadOperation& operation = *m_sceneReloadOperation;
        if (!operation.future.valid())
        {
            m_sceneReloadOperation.reset();
            m_isScriptActionActive = false;
            return;
        }

        const auto status =
            operation.future.wait_for(std::chrono::seconds(0));
        if (status != std::future_status::ready)
        {
            return;
        }

        try
        {
            operation.future.get();
        }
        catch (...)
        {
            finish_background_operation(
                operation,
                Result::fail(Code::UnknownError, Severity::Error,
                    "Scene reload job failed with an exception."));
        }

        Result result = Result::ok();
        if (operation.succeeded)
        {
            result = apply_background_scene_reload(operation);
        }
        else
        {
            result = Result::fail(
                operation.resultCode,
                operation.resultSeverity,
                operation.errorMessage.empty()
                ? std::string_view("Scene reload job failed.")
                : std::string_view(operation.errorMessage));
        }

        if (!result)
        {
            log_result("Failed to reload scene in background", result);
            set_status_message(
                result.message.empty()
                ? "シーン再読み込みに失敗しました。"
                : std::string(result.message),
                true);
        }
        else
        {
            set_status_message("シーンを読み込みました。", false);
        }

        operation.hasApplied = true;
        m_sceneReloadOperation.reset();
        m_isScriptActionActive = false;
    }

    Result EditorManager::apply_background_scene_reload(
        SceneReloadOperation& a_operation)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
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

        std::vector<Core::IO::Path> texturePaths{};
        std::vector<Core::IO::Path> modelPaths{};
        std::vector<Core::IO::Path> materialPaths{};
        {
            std::lock_guard<std::mutex> lock(a_operation.mutex);
            texturePaths = a_operation.texturePaths;
            modelPaths = a_operation.modelPaths;
            materialPaths = a_operation.materialPaths;
            a_operation.detail = "アセットを登録中...";
        }

        result = register_prepared_assets(
            *m_engine,
            *m_fileSystem,
            texturePaths,
            modelPaths,
            materialPaths);
        if (!result)
        {
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(a_operation.mutex);
            a_operation.detail = "Scene を World に反映中...";
        }

        return load_scene_to_world(Core::IO::Path(m_currentScenePath), true);
    }

    Result EditorManager::unload_current_scene()
    {
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        m_engine->set_editor_scene_id(GameCore::k_invalidSceneId);
        if (m_currentSceneId == GameCore::k_invalidSceneId &&
            m_loadedEditorScenes.empty())
        {
            return Result::ok();
        }

        Result result = Result::ok();
        if (!m_loadedEditorScenes.empty())
        {
            for (const LoadedSceneEntry& entry : m_loadedEditorScenes)
            {
                if (entry.sceneId == GameCore::k_invalidSceneId)
                {
                    continue;
                }

                result = m_engine->game_world()->unload_scene(entry.sceneId);
                if (!result && result.code != Code::NotFound)
                {
                    return result;
                }
            }
        }
        else if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            result = m_engine->game_world()->unload_scene(m_currentSceneId);
            if (!result)
            {
                return result;
            }
        }

        result = m_engine->game_world()->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        m_currentSceneId = GameCore::k_invalidSceneId;
        m_loadedSceneAsset = {};
        m_loadedEditorScenes.clear();
        m_selectedEntityId = GameCore::k_invalidEntityId;
        m_selectedSceneId = GameCore::k_invalidSceneId;
        return Result::ok();
    }

    Result EditorManager::load_scene_to_world(
        const Core::IO::Path& a_scenePath,
        bool a_isPrimaryScene)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        const Core::IO::Path scenePath = a_scenePath.normalize();
        if (scenePath.extension() != ".cuescene")
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene file extension must be .cuescene.");
        }
        if (is_scene_path_loaded(scenePath))
        {
            return Result::fail(Code::InvalidArgument, Severity::Warning,
                "Scene is already loaded in EditorWorld.");
        }

        GameCore::SceneAsset sceneAsset{};
        GameCore::SceneSerializer::LoadOptions loadOptions{};
        loadOptions.assetManager = &m_engine->asset_manager();
        Result result = GameCore::SceneSerializer::load_scene_asset(
            *m_fileSystem,
            scenePath,
            sceneAsset,
            loadOptions);
        if (!result)
        {
            return result;
        }

        Core::IO::Path assetRootPath(m_assetRootPath);
        if (assetRootPath.is_empty())
        {
            ProjectSettings projectSettings{};
            result = load_project_settings(
                *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
            if (!result)
            {
                return result;
            }
            assetRootPath = resolve_asset_root(
                Core::IO::Path(m_projectPath), projectSettings);
        }

        result = ensure_scene_asset_models_loaded(
            *m_engine, *m_fileSystem, assetRootPath, sceneAsset);
        if (!result)
        {
            return result;
        }

        auto storedSceneAsset =
            std::make_unique<GameCore::SceneAsset>(std::move(sceneAsset));
        GameCore::GameWorld::LoadSceneResult loadResult{};
        result = m_engine->game_world()->load_scene(
            *storedSceneAsset,
            loadResult);
        if (!result)
        {
            return result;
        }

        LoadedSceneEntry entry{};
        entry.sceneId = loadResult.sceneId;
        entry.path = scenePath.utf8();
        entry.name = storedSceneAsset->name().empty()
            ? scenePath.stem()
            : storedSceneAsset->name();

        if (a_isPrimaryScene)
        {
            m_currentSceneId = loadResult.sceneId;
            m_currentScenePath = scenePath.utf8();
            m_loadedSceneAsset = *storedSceneAsset;
            m_engine->set_editor_scene_id(m_currentSceneId);
        }

        entry.asset = std::move(storedSceneAsset);
        m_selectedSceneId = loadResult.sceneId;
        m_loadedEditorScenes.push_back(std::move(entry));
        m_selectedEntityId = GameCore::k_invalidEntityId;
        return Result::ok();
    }

    Result EditorManager::collect_project_scene_paths(
        std::vector<Core::IO::Path>& a_outScenePaths) const
    {
        a_outScenePaths.clear();
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "FileSystem が初期化されていません。");
        }
        if (m_assetRootPath.is_empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "Assets フォルダが設定されていません。");
        }

        const Core::IO::Path sceneRoot = Core::IO::Path::join(
            m_assetRootPath,
            Core::IO::Path("Scenes"));
        bool sceneRootExists = false;
        Result result = m_fileSystem->exists(sceneRoot, &sceneRootExists);
        if (!result)
        {
            return result;
        }
        if (!sceneRootExists)
        {
            return Result::ok();
        }

        std::vector<Core::IO::Path> entries{};
        result = m_fileSystem->list_directory(sceneRoot, &entries);
        if (!result)
        {
            return result;
        }

        for (const Core::IO::Path& entryPath : entries)
        {
            Core::IO::FileStat stat{};
            result = m_fileSystem->stat(entryPath, &stat);
            if (!result)
            {
                return result;
            }
            if (stat.type == Core::IO::FileType::regular &&
                entryPath.extension() == ".cuescene")
            {
                a_outScenePaths.push_back(entryPath.normalize());
            }
        }

        std::sort(a_outScenePaths.begin(), a_outScenePaths.end(),
            [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
            {
                return a_left.filename() < a_right.filename();
            });
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

    Result EditorManager::bake_current_scene_navigation()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }
        if (m_projectPath.empty() ||
            m_currentSceneId == GameCore::k_invalidSceneId ||
            m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no loaded scene to bake navigation.");
        }
        if (m_engine->is_playing())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "Navigation bake is not available during Play.");
        }

        Result result = drain_pending_editor_commands();
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

        const Core::IO::Path assetRoot = resolve_asset_root(
            Core::IO::Path(m_projectPath), projectSettings);
        const Core::IO::Path navMeshDirectory = Core::IO::Path::join(
            assetRoot, Core::IO::Path("Navigation"));
        result = m_fileSystem->create_directories(navMeshDirectory);
        if (!result)
        {
            return result;
        }

        const std::string sceneStem = Core::IO::Path(m_currentScenePath).stem();
        const std::string navMeshFileName = sceneStem + ".cuenavmesh";
        const Core::IO::Path navMeshRelativePath = Core::IO::Path::join(
            Core::IO::Path("Navigation"), Core::IO::Path(navMeshFileName));
        const Core::IO::Path navMeshPath = Core::IO::Path::join(
            assetRoot, navMeshRelativePath);

        std::vector<ECS::Entity> sourceEntities{};
        result = m_engine->game_world()->for_each_object_in_scene(
            m_currentSceneId,
            [&sourceEntities](GameCore::EntityId a_entityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
            {
                ECS::NavMeshBakeSourceComponent* source = nullptr;
                if (a_object.get_component(source) && source != nullptr &&
                    source->isIncluded)
                {
                    sourceEntities.push_back(a_entityId);
                }
            });
        if (!result)
        {
            return result;
        }
        if (sourceEntities.empty())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                "Scene has no NavMeshBakeSourceComponent.");
        }

        std::unordered_set<std::string> requiredModelNames{};
        result = m_engine->game_world()->for_each_object_in_scene(
            m_currentSceneId,
            [&requiredModelNames](GameCore::EntityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
            {
                ECS::NavMeshBakeSourceComponent* source = nullptr;
                if (!a_object.get_component(source) || source == nullptr ||
                    !source->isIncluded)
                {
                    return;
                }

                ECS::MeshFilterComponent* meshFilter = nullptr;
                if (a_object.get_component(meshFilter) &&
                    meshFilter != nullptr && !meshFilter->modelName.empty())
                {
                    requiredModelNames.insert(meshFilter->modelName);
                }
            });
        if (!result)
        {
            return result;
        }

        for (const std::string& modelName : requiredModelNames)
        {
            result = ensure_project_model_loaded(
                *m_engine, *m_fileSystem, assetRoot, modelName);
            if (!result)
            {
                return result;
            }
        }

        ECS::ECSManager* ecs = nullptr;
        result = m_engine->game_world()->ecs(ecs);
        if (!result || ecs == nullptr)
        {
            return result ? Result::fail(Code::InvalidState, Severity::Error,
                "ECS is not initialized.") : result;
        }

        GameCore::NavMeshAssetData navMeshAsset{};
        GameCore::NavMeshHandle navMeshHandle{};
        result = GameCore::NavigationBakePipeline::bake_entities_to_file_and_world(
            *ecs,
            m_engine->asset_manager(),
            *m_fileSystem,
            m_engine->game_world()->navigation_world(),
            std::span<const ECS::Entity>(sourceEntities),
            make_default_nav_mesh_settings(),
            navMeshPath,
            navMeshAsset,
            navMeshHandle);
        if (!result)
        {
            return result;
        }

        result = m_engine->game_world()->set_active_navigation_mesh(
            navMeshHandle, navMeshAsset);
        if (!result)
        {
            return result;
        }

        m_loadedSceneAsset.set_navigation_mesh_path(
            navMeshRelativePath.utf8());
        result = save_current_scene();
        if (!result)
        {
            return result;
        }

        set_status_message("NavMesh を Bake しました。", false);
        return Result::ok();
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

    void EditorManager::draw_navigation_debug_window()
    {
        if (!m_showNavigationDebugWindow)
        {
            return;
        }

        if (!ImGui::Begin("Navigation Debug", &m_showNavigationDebugWindow))
        {
            ImGui::End();
            return;
        }

        GameCore::GameWorld* world =
            m_engine != nullptr ? m_engine->active_world() : nullptr;
        if (world == nullptr)
        {
            ImGui::TextUnformatted("GameWorld が初期化されていません。");
            ImGui::End();
            return;
        }

        const GameCore::NavMeshHandle activeNavMesh =
            world->active_navigation_mesh();
        ImGui::Text("Active NavMesh: %s",
            activeNavMesh.valid() ? "true" : "false");
        ImGui::Text("Scene NavMesh: %s",
            m_loadedSceneAsset.navigation_mesh_path().empty()
            ? "(none)"
            : m_loadedSceneAsset.navigation_mesh_path().c_str());

        GameCore::NavMeshDebugGeometry geometry{};
        const Result geometryResult =
            world->build_navigation_debug_geometry(geometry);
        if (geometryResult)
        {
            ImGui::Text("Polygons: %zu", geometry.triangles.size());
            ImGui::Text("Edges: %zu", geometry.polygonEdges.size());
            ImGui::Text("Path lines: %zu", geometry.pathLines.size());
        }
        else
        {
            ImGui::Text("Debug Geometry: %s",
                geometryResult.message.data());
        }

        size_t sourceCount = 0;
        size_t agentCount = 0;
        if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            (void)world->for_each_object_in_scene(
                m_currentSceneId,
                [&sourceCount, &agentCount](GameCore::EntityId,
                    GameCore::SceneId,
                    GameCore::GameObject& a_object)
                {
                    ECS::NavMeshBakeSourceComponent* source = nullptr;
                    if (a_object.get_component(source) && source != nullptr &&
                        source->isIncluded)
                    {
                        ++sourceCount;
                    }

                    ECS::NavAgentComponent* agent = nullptr;
                    if (a_object.get_component(agent) && agent != nullptr)
                    {
                        ++agentCount;
                    }
                });
        }
        ImGui::Separator();
        ImGui::Text("Bake Sources: %zu", sourceCount);
        ImGui::Text("Agents: %zu", agentCount);

        if (m_selectedEntityId != GameCore::k_invalidEntityId)
        {
            ECS::NavAgentComponent* agent = nullptr;
            if (world->get_component(m_selectedEntityId, agent) && agent != nullptr)
            {
                ImGui::Separator();
                ImGui::Text("Selected NavAgent: %u", m_selectedEntityId);
                ImGui::Text("hasDestination: %s",
                    agent->hasDestination ? "true" : "false");
                ImGui::Text("hasPath: %s", agent->hasPath ? "true" : "false");
                ImGui::Text("hasArrived: %s",
                    agent->hasArrived ? "true" : "false");
                ImGui::Text("hasPathFailed: %s",
                    agent->hasPathFailed ? "true" : "false");
                ImGui::Text("isOnNavMesh: %s",
                    agent->isOnNavMesh ? "true" : "false");

                float destination[3] = {
                    agent->destination.x,
                    agent->destination.y,
                    agent->destination.z
                };
                if (ImGui::InputFloat3("Destination", destination))
                {
                    agent->destination = Math::float3(
                        destination[0], destination[1], destination[2]);
                }
                if (ImGui::Button("Set Destination"))
                {
                    const Result result = world->set_nav_agent_destination(
                        m_selectedEntityId,
                        Math::float3(
                            destination[0], destination[1], destination[2]));
                    if (!result)
                    {
                        log_result("Failed to set nav agent destination", result);
                        set_status_message("Agent 目標設定に失敗しました。", true);
                    }
                }
            }
        }

        ImGui::End();
    }

    void EditorManager::draw_status_bar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
        ImGui::BeginChild(
            "EditorStatusBar",
            ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing()),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (!m_statusMessage.empty())
        {
            if (m_hasStatusError)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    IM_COL32(255, 96, 96, 255));
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
            if (!m_statusMessage.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted("|");
                ImGui::SameLine();
            }

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

            ImGui::TextUnformatted(m_scriptBuildNotificationTitle.c_str());
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

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void EditorManager::draw_play_controls()
    {
        const bool isPlaying = m_engine != nullptr && m_engine->is_playing();
        const bool canStartPlay =
            m_engine != nullptr && !m_projectPath.empty() &&
            !m_isScriptActionActive && !isPlaying;
        const bool canStopPlay =
            m_engine != nullptr && !m_isScriptActionActive && isPlaying;

        const float buttonSide = ImGui::GetFrameHeight();
        const ImVec2 buttonSize(buttonSide, buttonSide);
        const float spacingX = ImGui::GetStyle().ItemSpacing.x;
        const float groupWidth = buttonSide * 3.0f + spacingX * 2.0f;
        const float contentMinX = ImGui::GetWindowContentRegionMin().x;
        const float contentMaxX = ImGui::GetWindowContentRegionMax().x;
        const float centerX =
            contentMinX + ((contentMaxX - contentMinX) - groupWidth) * 0.5f;
        const float cursorX = ImGui::GetCursorPosX();
        if (cursorX < centerX)
        {
            ImGui::SetCursorPosX(centerX);
        }

        ImGui::BeginDisabled(!canStartPlay);
        if (ImGui::Button(CUE_ICON_PLAY "##MenuPlay", buttonSize))
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
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Play");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!canStopPlay);
        if (ImGui::Button(CUE_ICON_PAUSE "##MenuPause", buttonSize))
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
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Pause (Stop)");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!canStopPlay);
        if (ImGui::Button(CUE_ICON_STOP "##MenuStop", buttonSize))
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
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Stop (Exit)");
        }
    }

    void EditorManager::draw_script_build_configuration_combo()
    {
        const bool canEditBuildSettings =
            !m_projectPath.empty() && !m_isScriptActionActive;
        const char* currentLabel =
            BuildSystem::to_configuration_name(m_scriptBuildConfiguration);

        ImGui::SetNextItemWidth(120.0f);
        ImGui::BeginDisabled(!canEditBuildSettings);
        if (ImGui::BeginCombo("##ScriptBuildConfiguration", currentLabel))
        {
            const auto drawConfigurationItem =
                [this](
                    const char* a_label,
                    BuildConfiguration a_configuration)
            {
                const bool isSelected =
                    m_scriptBuildConfiguration == a_configuration;
                if (ImGui::Selectable(a_label, isSelected) && !isSelected)
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
                            std::string("GameScript のビルド構成を ") +
                                a_label + " に変更しました。",
                            false);
                    }
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            };

            drawConfigurationItem("Debug", BuildConfiguration::Debug);
            drawConfigurationItem(
                "RelWithDebInfo",
                BuildConfiguration::RelWithDebInfo);
            drawConfigurationItem("Release", BuildConfiguration::Release);

            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("GameScript ビルド構成");
        }
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

    void EditorManager::draw_game_release_app_settings_popup()
    {
        if (m_openGameReleaseAppSettingsPopup)
        {
            ImGui::OpenPopup("Game Release App Settings");
            m_openGameReleaseAppSettingsPopup = false;
        }

        if (!ImGui::BeginPopupModal(
                "Game Release App Settings",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("EXE 名");
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText(
            "##GameReleaseExecutableName",
            m_gameReleaseExecutableNameBuffer.data(),
            m_gameReleaseExecutableNameBuffer.size());
        ImGui::SameLine();
        ImGui::TextUnformatted(".exe");

        ImGui::TextUnformatted("タイトルバー");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText(
            "##GameReleaseWindowTitle",
            m_gameReleaseWindowTitleBuffer.data(),
            m_gameReleaseWindowTitleBuffer.size());

        ImGui::TextUnformatted("アプリアイコン");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(
            "##GameReleaseIconPath",
            m_gameReleaseIconPathBuffer.data(),
            m_gameReleaseIconPathBuffer.size());
        ImGui::TextDisabled(
            "プロジェクト内の .png/.jpg/.bmp/.ico を指定できます。");
        const std::string selectedIconExtension =
            to_lower_ascii(m_selectedAssetPath.extension());
        const bool canUseSelectedIcon =
            !m_selectedAssetPath.is_empty() &&
            (selectedIconExtension == ".png" ||
                selectedIconExtension == ".jpg" ||
                selectedIconExtension == ".jpeg" ||
                selectedIconExtension == ".bmp" ||
                selectedIconExtension == ".ico");
        ImGui::BeginDisabled(!canUseSelectedIcon);
        if (ImGui::Button("選択中の画像を使用"))
        {
            const std::string projectRoot =
                Core::IO::Path(m_projectPath).normalize().utf8();
            const std::string selectedPath =
                m_selectedAssetPath.normalize().utf8();
            std::string iconPath = selectedPath;
            if (!projectRoot.empty() &&
                selectedPath.rfind(projectRoot + "/", 0) == 0)
            {
                iconPath = selectedPath.substr(projectRoot.size() + 1);
            }

            set_text_buffer(m_gameReleaseIconPathBuffer, iconPath);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        if (ImGui::Button("保存"))
        {
            const Result result = save_game_release_app_settings(
                m_gameReleaseExecutableNameBuffer.data(),
                m_gameReleaseWindowTitleBuffer.data(),
                m_gameReleaseIconPathBuffer.data());
            if (!result)
            {
                log_result("Failed to save game release app settings", result);
                set_status_message(
                    result.message.empty()
                    ? "ゲーム配布アプリ設定の保存に失敗しました。"
                    : std::string(result.message),
                    true);
            }
            else
            {
                set_status_message(
                    "ゲーム配布アプリ設定を保存しました。", false);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            const Result result = load_game_release_app_settings_to_buffers();
            if (!result)
            {
                log_result("Failed to reload game release app settings", result);
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::draw_background_progress_window()
    {
        if (m_sceneReloadOperation == nullptr)
        {
            return;
        }

        SceneReloadOperation& operation = *m_sceneReloadOperation;
        const uint32_t total =
            (std::max)(operation.total.load(std::memory_order_relaxed), 1u);
        const uint32_t completed = (std::min)(
            operation.completed.load(std::memory_order_relaxed),
            total);
        const float progress =
            static_cast<float>(completed) / static_cast<float>(total);

        std::string title{};
        std::string detail{};
        {
            std::lock_guard<std::mutex> lock(operation.mutex);
            title = operation.title;
            detail = operation.detail;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                viewport->WorkPos.y + viewport->WorkSize.y - 96.0f),
            ImGuiCond_Always,
            ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking;
        if (!ImGui::Begin("バックグラウンド処理", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(title.c_str());
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        ImGui::TextWrapped("%s", detail.c_str());

        ImGui::End();
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

    void EditorManager::draw_add_menu_items()
    {
        const GameCore::SceneId targetSceneId = selected_add_scene_id();
        const bool canAddObject =
            m_bridge != nullptr && !m_isScriptActionActive &&
            targetSceneId != GameCore::k_invalidSceneId;

        if (ImGui::BeginMenu("3D", canAddObject))
        {
            if (ImGui::MenuItem("カメラを追加"))
            {
                const Result result = m_bridge->submit_command(
                    std::make_unique<AddObjectCommand>(
                        AddObjectType::Camera, targetSceneId));
                if (!result)
                {
                    log_result("Failed to add camera object", result);
                    set_status_message("カメラの追加に失敗しました。", true);
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
                        AddObjectType::StaticMesh3D, targetSceneId));
                if (!result)
                {
                    log_result("Failed to add 3D object", result);
                    set_status_message(
                        "3D オブジェクトの追加に失敗しました。", true);
                }
                else
                {
                    set_status_message("3D オブジェクトを追加しました。", false);
                }
            }

            if (ImGui::BeginMenu("ライト"))
            {
                auto addLight =
                    [this, targetSceneId](
                        AddObjectType a_type,
                        const char* a_logLabel,
                        const char* a_successMessage,
                        const char* a_failureMessage)
                {
                    const Result result = m_bridge->submit_command(
                        std::make_unique<AddObjectCommand>(
                            a_type,
                            targetSceneId));
                    if (!result)
                    {
                        log_result(a_logLabel, result);
                        set_status_message(a_failureMessage, true);
                    }
                    else
                    {
                        set_status_message(a_successMessage, false);
                    }
                };

                if (ImGui::MenuItem("Directional Light"))
                {
                    addLight(
                        AddObjectType::DirectionalLight,
                        "Failed to add directional light",
                        "Directional Light を追加しました。",
                        "Directional Light の追加に失敗しました。");
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    addLight(
                        AddObjectType::PointLight,
                        "Failed to add point light",
                        "Point Light を追加しました。",
                        "Point Light の追加に失敗しました。");
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    addLight(
                        AddObjectType::SpotLight,
                        "Failed to add spot light",
                        "Spot Light を追加しました。",
                        "Spot Light の追加に失敗しました。");
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("2D", canAddObject))
        {
            if (ImGui::MenuItem("オブジェクトを追加"))
            {
                const Result result = m_bridge->submit_command(
                    std::make_unique<AddObjectCommand>(
                        AddObjectType::Sprite2D, targetSceneId));
                if (!result)
                {
                    log_result("Failed to add 2D object", result);
                    set_status_message(
                        "2D オブジェクトの追加に失敗しました。", true);
                }
                else
                {
                    set_status_message("2D オブジェクトを追加しました。", false);
                }
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(
                "マテリアルを追加", nullptr, false,
                !m_assetRootPath.is_empty() && !m_isScriptActionActive))
        {
            const Result result = create_material_asset();
            if (!result)
            {
                log_result("Failed to create material asset", result);
                set_status_message("マテリアルの追加に失敗しました。", true);
            }
            else
            {
                set_status_message("マテリアルを追加しました。", false);
            }
        }

        if (ImGui::MenuItem(
                "GameScript を追加", nullptr, false,
                !m_projectPath.empty() && !m_isScriptActionActive))
        {
            m_createScriptNameBuffer.fill('\0');
            m_openCreateScriptPopup = true;
        }

        draw_main_camera_menu();
    }

    void EditorManager::draw_view_menu_items()
    {
        bool isGridVisible =
            m_engine != nullptr && m_engine->is_debug_grid_visible();
        ImGui::BeginDisabled(m_engine == nullptr);
        if (ImGui::Checkbox("グリッドを表示", &isGridVisible) &&
            m_engine != nullptr)
        {
            m_engine->set_debug_grid_visible(isGridVisible);
        }

        DrawSystem::DebugViewShadingMode shadingMode =
            m_engine != nullptr
            ? m_engine->debug_view_shading_mode()
            : DrawSystem::DebugViewShadingMode::MaterialLighting;
        if (ImGui::BeginMenu("描画モード"))
        {
            if (ImGui::MenuItem("ソリッド", nullptr,
                    shadingMode == DrawSystem::DebugViewShadingMode::Solid) &&
                m_engine != nullptr)
            {
                m_engine->set_debug_view_shading_mode(
                    DrawSystem::DebugViewShadingMode::Solid);
            }
            if (ImGui::MenuItem("マテリアル", nullptr,
                    shadingMode == DrawSystem::DebugViewShadingMode::Material) &&
                m_engine != nullptr)
            {
                m_engine->set_debug_view_shading_mode(
                    DrawSystem::DebugViewShadingMode::Material);
            }
            if (ImGui::MenuItem("ライティング", nullptr,
                    shadingMode == DrawSystem::DebugViewShadingMode::Lighting) &&
                m_engine != nullptr)
            {
                m_engine->set_debug_view_shading_mode(
                    DrawSystem::DebugViewShadingMode::Lighting);
            }
            if (ImGui::MenuItem("レンダー", nullptr,
                    shadingMode ==
                        DrawSystem::DebugViewShadingMode::MaterialLighting) &&
                m_engine != nullptr)
            {
                m_engine->set_debug_view_shading_mode(
                    DrawSystem::DebugViewShadingMode::MaterialLighting);
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Checkbox("SpotShadowMap Preview", &m_showSpotShadowMapPreview);
    }

    void EditorManager::draw_scene_menu_items()
    {
        ImGui::TextUnformatted("読み込み済みシーン");
        ImGui::Separator();

        if (m_loadedEditorScenes.empty())
        {
            ImGui::TextDisabled("読み込まれているシーンはありません。");
        }
        else
        {
            for (const LoadedSceneEntry& entry : m_loadedEditorScenes)
            {
                ImGui::PushID(static_cast<int>(entry.sceneId));
                const bool isPrimary = entry.sceneId == m_currentSceneId;
                ImGui::Text(
                    "%s%s",
                    entry.name.empty() ? "<unnamed>" : entry.name.c_str(),
                    isPrimary ? " (Current)" : "");
                ImGui::PopID();
            }
        }

        ImGui::Separator();
        const bool canLoadScene =
            m_engine != nullptr && !m_engine->is_playing() &&
            !m_projectPath.empty() && !m_assetRootPath.is_empty();
        ImGui::BeginDisabled(!canLoadScene);
        if (ImGui::Button("読込", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::OpenPopup("LoadScenePopup");
        }
        ImGui::EndDisabled();

        if (ImGui::BeginPopup("LoadScenePopup"))
        {
            std::vector<Core::IO::Path> scenePaths{};
            const Result collectResult = collect_project_scene_paths(scenePaths);
            if (!collectResult)
            {
                ImGui::TextWrapped(
                    "シーン一覧の取得に失敗しました: %s",
                    collectResult.message.data());
            }
            else if (scenePaths.empty())
            {
                ImGui::TextDisabled(
                    "Assets/Scenes に .cuescene が見つかりません。");
            }
            else
            {
                for (const Core::IO::Path& scenePath : scenePaths)
                {
                    const std::string label = scenePath.filename();
                    const bool isLoaded = is_scene_path_loaded(scenePath);
                    const std::string itemLabel =
                        isLoaded ? label + " (読み込み済み)" : label;
                    if (ImGui::MenuItem(
                            itemLabel.c_str(), nullptr, false, !isLoaded))
                    {
                        Result result = drain_pending_editor_commands();
                        if (result)
                        {
                            result = load_scene_to_world(scenePath, false);
                        }

                        if (!result)
                        {
                            log_result("Failed to load scene additively", result);
                            set_status_message(
                                "シーンの追加読み込みに失敗しました。", true);
                        }
                        else
                        {
                            set_status_message(
                                "シーンをワールドに追加しました。", false);
                        }
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    void EditorManager::draw_display_menu_items()
    {
        const auto drawWindowItem =
            [this](
                const char* a_label,
                const char* a_windowName,
                bool* a_showWindow = nullptr,
                bool a_isEnabled = true)
        {
            if (ImGui::MenuItem(a_label, nullptr, false, a_isEnabled))
            {
                show_and_focus_window(a_windowName, a_showWindow);
            }
        };

        drawWindowItem("GameView", "GameView", nullptr, m_gameView != nullptr);
        drawWindowItem("DebugView", "DebugView", nullptr, m_debugView != nullptr);
        drawWindowItem(
            "Asset Browser",
            "Asset Browser",
            nullptr,
            m_assetBrowser != nullptr);
        drawWindowItem(
            "ヒエラルキー",
            "ヒエラルキー",
            nullptr,
            m_hierarchy != nullptr);
        drawWindowItem(
            "インスペクター",
            "インスペクター",
            nullptr,
            m_inspector != nullptr);
        drawWindowItem(
            "Frame Statistics",
            "Frame Statistics",
            nullptr,
            m_statistics != nullptr);

        ImGui::Separator();
        drawWindowItem(
            "Script Build Output",
            "Script Build Output",
            &m_showScriptBuildOutput);
        drawWindowItem(
            "Navigation Debug",
            "Navigation Debug",
            &m_showNavigationDebugWindow,
            m_engine != nullptr);
    }

    void EditorManager::show_and_focus_window(
        const char* a_windowName,
        bool* a_showWindow)
    {
        if (a_windowName == nullptr)
        {
            return;
        }

        if (a_showWindow != nullptr)
        {
            *a_showWindow = true;
        }
        m_pendingFocusWindowName = a_windowName;
    }

    void EditorManager::focus_pending_window()
    {
        if (m_pendingFocusWindowName.empty())
        {
            return;
        }

        ImGui::SetWindowFocus(m_pendingFocusWindowName.c_str());
        m_pendingFocusWindowName.clear();
    }

    GameCore::SceneId EditorManager::selected_add_scene_id() const noexcept
    {
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return GameCore::k_invalidSceneId;
        }

        if (m_selectedEntityId != GameCore::k_invalidEntityId)
        {
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
            if (m_engine->game_world()->source_scene_id(
                    m_selectedEntityId, sceneId) &&
                sceneId != GameCore::k_invalidSceneId)
            {
                return sceneId;
            }
        }

        if (m_selectedSceneId != GameCore::k_invalidSceneId)
        {
            return m_selectedSceneId;
        }

        return m_currentSceneId;
    }

    std::vector<Hierarchy::SceneEntry>
        EditorManager::collect_hierarchy_scenes() const
    {
        std::vector<Hierarchy::SceneEntry> scenes{};
        scenes.reserve(m_loadedEditorScenes.size());

        for (const LoadedSceneEntry& entry : m_loadedEditorScenes)
        {
            if (entry.sceneId == GameCore::k_invalidSceneId)
            {
                continue;
            }

            scenes.push_back(Hierarchy::SceneEntry{
                entry.name,
                entry.sceneId,
                entry.sceneId == m_currentSceneId
            });
        }

        return scenes;
    }

    bool EditorManager::is_scene_path_loaded(
        const Core::IO::Path& a_scenePath) const noexcept
    {
        const std::string scenePath = a_scenePath.normalize().utf8();
        if (scenePath.empty())
        {
            return false;
        }

        return std::any_of(
            m_loadedEditorScenes.begin(),
            m_loadedEditorScenes.end(),
            [&scenePath](const LoadedSceneEntry& a_entry)
            {
                return Core::IO::Path(a_entry.path).normalize().utf8() ==
                    scenePath;
            });
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

    void EditorManager::draw_skybox_menu()
    {
        if (!ImGui::BeginMenu("Skybox"))
        {
            return;
        }

        const bool canSelectSkybox =
            m_engine != nullptr && m_fileSystem != nullptr &&
            m_backend != nullptr &&
            m_backend->get_view_manager() != nullptr &&
            !m_assetRootPath.is_empty();
        ImGui::BeginDisabled(!canSelectSkybox);

        const bool isNoneSelected =
            m_engine == nullptr || m_engine->skybox_texture_id() == 0xffffffffu;
        if (ImGui::MenuItem("なし", nullptr, isNoneSelected, canSelectSkybox) &&
            m_engine != nullptr)
        {
            m_engine->clear_skybox_texture();
            set_status_message("Skybox を解除しました。", false);
        }

        ImGui::Separator();

        uint32_t cubeTextureCount = 0;
        if (canSelectSkybox)
        {
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                m_assetRootPath,
                Core::IO::Path("Textures"));
            bool textureRootExists = false;
            Result result = m_fileSystem->exists(textureRoot, &textureRootExists);
            if (result && textureRootExists)
            {
                std::vector<Core::IO::Path> texturePaths{};
                result = m_fileSystem->list_directory(textureRoot, &texturePaths);
                if (result)
                {
                    std::sort(texturePaths.begin(), texturePaths.end(),
                        [](const Core::IO::Path& a_left,
                            const Core::IO::Path& a_right)
                        {
                            return a_left.utf8() < a_right.utf8();
                        });

                    for (const Core::IO::Path& texturePath : texturePaths)
                    {
                        if (!is_cube_cuetexture_file(*m_fileSystem, texturePath))
                        {
                            continue;
                        }

                        ++cubeTextureCount;
                        const std::string textureName =
                            make_asset_relative_name(texturePath);
                        const bool isSelected =
                            m_engine->skybox_texture_name() == textureName;
                        if (ImGui::MenuItem(
                                textureName.c_str(),
                                nullptr,
                                isSelected,
                                true) &&
                            !isSelected)
                        {
                            uint32_t textureId = AssetManager::k_errorTextureId;
                            result = m_engine->asset_manager()
                                .register_texture_from_cuetexture(
                                    *m_fileSystem,
                                    textureName,
                                    texturePath,
                                    textureId);
                            if (!result)
                            {
                                log_result("Failed to set skybox texture", result);
                                set_status_message(
                                    "Skybox texture の設定に失敗しました。",
                                    true);
                            }
                            else
                            {
                                RHI::TextureHandle textureHandle{};
                                result = m_engine->asset_manager()
                                    .get_texture_handle(textureId, textureHandle);
                                if (!result)
                                {
                                    log_result(
                                        "Failed to get skybox texture handle",
                                        result);
                                    set_status_message(
                                        "Skybox texture の取得に失敗しました。",
                                        true);
                                    continue;
                                }

                                CueTextureHeader header{};
                                result = read_cuetexture_header(
                                    *m_fileSystem,
                                    texturePath,
                                    header);
                                if (!result)
                                {
                                    log_result(
                                        "Failed to read skybox texture header",
                                        result);
                                    set_status_message(
                                        "Skybox texture 情報の読み込みに失敗しました。",
                                        true);
                                    continue;
                                }

                                RHI::ViewHandle textureSrvHandle{};
                                const std::string viewName =
                                    "Skybox/" + textureName;
                                result = m_backend->get_view_manager()->get_view(
                                    viewName,
                                    textureSrvHandle);
                                if (!result)
                                {
                                    RHI::ViewDesc viewDesc{};
                                    viewDesc.name = viewName;
                                    viewDesc.type =
                                        RHI::ViewType::ShaderResourceTextureCube;
                                    viewDesc.bufferKind = RHI::BufferKind::Texture;
                                    viewDesc.textureHandle = textureHandle;
                                    viewDesc.colorFormat =
                                        static_cast<RHI::ColorFormat>(header.format);
                                    viewDesc.mipSlice = 0;
                                    viewDesc.mipLevels = header.mipCount;
                                    result = m_backend->get_view_manager()
                                        ->create_view(viewDesc, textureSrvHandle);
                                }

                                if (!result)
                                {
                                    log_result(
                                        "Failed to create skybox texture view",
                                        result);
                                    set_status_message(
                                        "Skybox texture view の作成に失敗しました。",
                                        true);
                                    continue;
                                }

                                m_engine->set_skybox_texture(
                                    textureId,
                                    textureSrvHandle,
                                    textureName);
                                set_status_message(
                                    "Skybox texture を設定しました。",
                                    false);
                            }
                        }
                    }
                }
            }
        }

        if (cubeTextureCount == 0)
        {
            ImGui::TextDisabled("CubeMap の .cuetexture がありません。");
        }

        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    void EditorManager::process_debug_pick_request()
    {
        if (m_debugView == nullptr || m_engine == nullptr)
        {
            return;
        }

        if (m_debugGizmoPickBlockFrames > 0)
        {
            GameCore::EntityId discardedEntityId = GameCore::k_invalidEntityId;
            (void)m_engine->consume_debug_pick_result(discardedEntityId);
            m_engine->cancel_debug_pick();
            m_debugView->clear_pick_request();
            m_hasPendingDebugPickFallback = false;
            --m_debugGizmoPickBlockFrames;
            return;
        }

        auto selectEntity =
            [this](GameCore::EntityId a_entityId)
        {
            m_selectedEntityId = a_entityId;
            m_selectedSceneId = GameCore::k_invalidSceneId;
            if (a_entityId != GameCore::k_invalidEntityId &&
                m_engine->game_world() != nullptr)
            {
                (void)m_engine->game_world()->source_scene_id(
                    a_entityId,
                    m_selectedSceneId);
            }
        };

        GameCore::EntityId pickedEntityId = GameCore::k_invalidEntityId;
        if (m_engine->consume_debug_pick_result(pickedEntityId))
        {
            if (pickedEntityId == GameCore::k_invalidEntityId &&
                m_hasPendingDebugPickFallback)
            {
                GameCore::EntityId debugEntityId = GameCore::k_invalidEntityId;
                if (pick_debug_non_rendered_object(
                    m_pendingDebugPickFallback,
                    debugEntityId))
                {
                    pickedEntityId = debugEntityId;
                }
            }

            selectEntity(pickedEntityId);
            m_hasPendingDebugPickFallback = false;
        }

        DebugView::PickRequest pickRequest{};
        if (!m_debugView->consume_pick_request(pickRequest))
        {
            return;
        }

        GameCore::EntityId debugEntityId = GameCore::k_invalidEntityId;
        if (pick_debug_non_rendered_object(pickRequest, debugEntityId))
        {
            m_engine->cancel_debug_pick();
            selectEntity(debugEntityId);
            m_hasPendingDebugPickFallback = false;
            return;
        }

        if (m_engine->request_debug_pick_pixel(
            pickRequest.pixelX,
            pickRequest.pixelY))
        {
            m_pendingDebugPickFallback = pickRequest;
            m_hasPendingDebugPickFallback = true;
        }
    }

    bool EditorManager::draw_debug_transform_gizmo(
        const ImVec2& a_viewportMin,
        const ImVec2& a_viewportMax,
        ImDrawList* a_drawList)
    {
        ImGuizmo::BeginFrame();
        if (m_debugView == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr || m_bridge == nullptr ||
            m_engine->is_playing() || m_isScriptActionActive)
        {
            m_isDebugGizmoEditing = false;
            m_debugGizmoEntityId = GameCore::k_invalidEntityId;
            return false;
        }

        const ImVec2 viewportSize(
            a_viewportMax.x - a_viewportMin.x,
            a_viewportMax.y - a_viewportMin.y);
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f ||
            m_selectedEntityId == GameCore::k_invalidEntityId)
        {
            return false;
        }

        GameCore::GameWorld* debugWorld = m_engine->game_world();
        ECS::TransformComponent* transform = nullptr;
        const Result transformResult =
            debugWorld->get_component<ECS::TransformComponent>(
                m_selectedEntityId,
                transform);
        if (!transformResult || transform == nullptr)
        {
            return false;
        }

        const ImGuiWindowFlags toolbarFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking;
        ImGui::SetNextWindowPos(
            ImVec2(a_viewportMin.x + 8.0f, a_viewportMin.y + 8.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        if (ImGui::Begin("DebugTransformGizmoToolbar", nullptr, toolbarFlags))
        {
            draw_gizmo_mode_button(
                "移動",
                static_cast<uint32_t>(ImGuizmo::TRANSLATE),
                m_debugGizmoOperation);
            ImGui::SameLine();
            draw_gizmo_mode_button(
                "回転",
                static_cast<uint32_t>(ImGuizmo::ROTATE),
                m_debugGizmoOperation);
            ImGui::SameLine();
            draw_gizmo_mode_button(
                "拡縮",
                static_cast<uint32_t>(ImGuizmo::SCALE),
                m_debugGizmoOperation);
            ImGui::SameLine();
            ImGui::TextUnformatted("|");
            ImGui::SameLine();
            draw_gizmo_mode_button(
                "World",
                static_cast<uint32_t>(ImGuizmo::WORLD),
                m_debugGizmoMode);
            ImGui::SameLine();
            draw_gizmo_mode_button(
                "Local",
                static_cast<uint32_t>(ImGuizmo::LOCAL),
                m_debugGizmoMode);
        }
        ImGui::End();

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            {
                m_debugGizmoOperation =
                    static_cast<uint32_t>(ImGuizmo::TRANSLATE);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            {
                m_debugGizmoOperation =
                    static_cast<uint32_t>(ImGuizmo::ROTATE);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                m_debugGizmoOperation =
                    static_cast<uint32_t>(ImGuizmo::SCALE);
            }
        }

        const GpuData::ViewProjectionGpu viewProjection =
            m_debugCamera.view_projection();
        Math::float4x4 objectMatrix = Math::make_affine_matrix(
            transform->scale,
            transform->rotation,
            transform->position);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(a_drawList);
        ImGuizmo::SetRect(
            a_viewportMin.x,
            a_viewportMin.y,
            viewportSize.x,
            viewportSize.y);

        const ImGuizmo::OPERATION operation =
            static_cast<ImGuizmo::OPERATION>(m_debugGizmoOperation);
        const ImGuizmo::MODE mode =
            static_cast<ImGuizmo::MODE>(m_debugGizmoMode);
        a_drawList->PushClipRect(a_viewportMin, a_viewportMax, true);
        const bool manipulated = ImGuizmo::Manipulate(
            &viewProjection.view.values[0][0],
            &viewProjection.projection.values[0][0],
            operation,
            mode,
            &objectMatrix.values[0][0]);
        a_drawList->PopClipRect();
        const bool isUsing = ImGuizmo::IsUsing();
        if (ImGuizmo::IsOver() || isUsing)
        {
            m_debugView->clear_pick_request();
            m_engine->cancel_debug_pick();
            m_hasPendingDebugPickFallback = false;
            m_debugGizmoPickBlockFrames = 2;
        }
        const bool isBlockingPick = ImGuizmo::IsOver() || isUsing;

        if (isUsing &&
            (!m_isDebugGizmoEditing ||
                m_debugGizmoEntityId != m_selectedEntityId))
        {
            m_debugGizmoStartTransform = *transform;
            m_debugGizmoEntityId = m_selectedEntityId;
            m_isDebugGizmoEditing = true;
        }

        if (manipulated)
        {
            float translation[3] = {};
            float rotationDegrees[3] = {};
            float scale[3] = {};
            ImGuizmo::DecomposeMatrixToComponents(
                &objectMatrix.values[0][0],
                translation,
                rotationDegrees,
                scale);
            const bool hasFiniteValues =
                std::isfinite(translation[0]) &&
                std::isfinite(translation[1]) &&
                std::isfinite(translation[2]) &&
                std::isfinite(rotationDegrees[0]) &&
                std::isfinite(rotationDegrees[1]) &&
                std::isfinite(rotationDegrees[2]) &&
                std::isfinite(scale[0]) &&
                std::isfinite(scale[1]) &&
                std::isfinite(scale[2]);
            if (hasFiniteValues)
            {
                ECS::TransformComponent nextTransform = *transform;
                nextTransform.position = Math::float3(
                    translation[0],
                    translation[1],
                    translation[2]);
                nextTransform.rotation = Math::degrees_to_radians(
                    Math::float3(
                        rotationDegrees[0],
                        rotationDegrees[1],
                        rotationDegrees[2]));
                nextTransform.scale = Math::float3(
                    scale[0],
                    scale[1],
                    scale[2]);
                *transform = nextTransform;
            }
        }

        if (!isUsing && m_isDebugGizmoEditing)
        {
            ECS::TransformComponent* currentTransform = nullptr;
            const Result currentResult =
                debugWorld->get_component<ECS::TransformComponent>(
                    m_debugGizmoEntityId,
                    currentTransform);
            if (currentResult && currentTransform != nullptr &&
                !transform_nearly_equal(
                    m_debugGizmoStartTransform,
                    *currentTransform))
            {
                const Result commandResult = m_bridge->submit_command(
                    std::make_unique<SetTransformComponentCommand>(
                        m_debugGizmoEntityId,
                        m_debugGizmoStartTransform,
                        *currentTransform));
                if (!commandResult)
                {
                    log_result(
                        "Failed to submit debug gizmo transform command",
                        commandResult);
                    set_status_message(
                        "DebugView の Transform 操作を履歴に追加できませんでした。",
                        true);
                }
            }

            m_isDebugGizmoEditing = false;
            m_debugGizmoEntityId = GameCore::k_invalidEntityId;
        }

        return isBlockingPick;
    }

    bool EditorManager::draw_debug_overlay(
        const ImVec2& a_viewportMin,
        const ImVec2& a_viewportMax,
        ImDrawList* a_drawList)
    {
        const bool isGizmoBlocking = draw_debug_transform_gizmo(
            a_viewportMin,
            a_viewportMax,
            a_drawList);
        const bool isPreviewBlocking = draw_spot_shadow_map_preview(
            a_viewportMin,
            a_viewportMax,
            a_drawList);
        return isGizmoBlocking || isPreviewBlocking;
    }

    bool EditorManager::draw_spot_shadow_map_preview(
        const ImVec2& a_viewportMin,
        const ImVec2& a_viewportMax,
        ImDrawList* a_drawList)
    {
        if (!m_showSpotShadowMapPreview || m_backend == nullptr ||
            a_drawList == nullptr)
        {
            return false;
        }

        RHI::IViewManager* viewManager = m_backend->get_view_manager();
        if (viewManager == nullptr)
        {
            return false;
        }

        RHI::ViewHandle viewHandle{};
        const Result viewResult =
            viewManager->get_view("SpotShadowMapPreviewSRV", viewHandle);
        if (!viewResult || !viewHandle.valid())
        {
            return false;
        }

        const uint32_t bufferIndex = m_backend->current_back_buffer_index();
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
            m_backend->get_gpu_descriptor_handle(
                viewHandle,
                bufferIndex,
                m_backend->buffer_count());
        if (gpuHandle.ptr == 0)
        {
            return false;
        }

        const ImVec2 viewportSize(
            a_viewportMax.x - a_viewportMin.x,
            a_viewportMax.y - a_viewportMin.y);
        if (viewportSize.x <= 80.0f || viewportSize.y <= 80.0f)
        {
            return false;
        }

        const float imageSize = std::clamp(
            (std::min)(viewportSize.x, viewportSize.y) * 0.28f,
            96.0f,
            240.0f);
        const float titleHeight = 22.0f;
        const float padding = 8.0f;
        const ImVec2 panelMin(
            a_viewportMax.x - imageSize - padding * 2.0f - 10.0f,
            a_viewportMin.y + 10.0f);
        const ImVec2 panelMax(
            panelMin.x + imageSize + padding * 2.0f,
            panelMin.y + imageSize + titleHeight + padding * 2.0f);
        const ImVec2 imageMin(
            panelMin.x + padding,
            panelMin.y + titleHeight + padding);
        const ImVec2 imageMax(
            imageMin.x + imageSize,
            imageMin.y + imageSize);

        a_drawList->AddRectFilled(
            panelMin,
            panelMax,
            IM_COL32(16, 16, 16, 210),
            4.0f);
        a_drawList->AddText(
            ImVec2(panelMin.x + padding, panelMin.y + 4.0f),
            IM_COL32(230, 230, 230, 255),
            "SpotShadowMap");
        a_drawList->AddImage(
            static_cast<ImTextureID>(gpuHandle.ptr),
            imageMin,
            imageMax,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, 255));
        a_drawList->AddRect(
            imageMin,
            imageMax,
            IM_COL32(255, 255, 255, 180),
            0.0f,
            0,
            1.0f);

        const GameCore::GameWorld* world =
            m_engine != nullptr ? m_engine->active_world() : nullptr;
        const Cue::ShadowSystem::ShadowFrameData* shadowFrame = nullptr;
        if (world != nullptr &&
            bufferIndex < world->shadow_frame_state().frameStates.size())
        {
            shadowFrame =
                &world->shadow_frame_state().frame_state(bufferIndex);
        }

        const float tileWidth =
            imageSize /
            static_cast<float>(GpuData::k_spotShadowAtlasColumnCount);
        const float tileHeight =
            imageSize /
            static_cast<float>(GpuData::k_spotShadowAtlasRowCount);
        for (uint32_t row = 0; row < GpuData::k_spotShadowAtlasRowCount; ++row)
        {
            for (uint32_t column = 0;
                 column < GpuData::k_spotShadowAtlasColumnCount;
                 ++column)
            {
                const uint32_t shadowIndex =
                    row * GpuData::k_spotShadowAtlasColumnCount + column;
                const ImVec2 tileMin(
                    imageMin.x + static_cast<float>(column) * tileWidth,
                    imageMin.y + static_cast<float>(row) * tileHeight);
                const ImVec2 tileMax(
                    tileMin.x + tileWidth,
                    tileMin.y + tileHeight);
                const bool hasShadow =
                    shadowFrame != nullptr &&
                    shadowIndex < shadowFrame->spotShadows.size() &&
                    shadowFrame->spotShadows[shadowIndex].params.x >= 0.5f;

                a_drawList->AddRect(
                    tileMin,
                    tileMax,
                    hasShadow
                        ? IM_COL32(80, 230, 255, 230)
                        : IM_COL32(120, 120, 120, 160),
                    0.0f,
                    0,
                    hasShadow ? 1.5f : 1.0f);

                char label[32]{};
                if (hasShadow)
                {
                    const uint32_t lightIndex = static_cast<uint32_t>(
                        shadowFrame->spotShadows[shadowIndex].params.w + 0.5f);
                    std::snprintf(
                        label,
                        sizeof(label),
                        "S%u L%u",
                        shadowIndex,
                        lightIndex);
                }
                else
                {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "S%u",
                        shadowIndex);
                }

                a_drawList->AddRectFilled(
                    tileMin,
                    ImVec2(tileMin.x + 48.0f, tileMin.y + 18.0f),
                    IM_COL32(0, 0, 0, 150),
                    2.0f);
                a_drawList->AddText(
                    ImVec2(tileMin.x + 4.0f, tileMin.y + 2.0f),
                    hasShadow
                        ? IM_COL32(220, 250, 255, 255)
                        : IM_COL32(180, 180, 180, 255),
                    label);
            }
        }

        return ImGui::IsMouseHoveringRect(panelMin, panelMax);
    }

    bool EditorManager::pick_debug_non_rendered_object(
        const DebugView::PickRequest& a_request,
        GameCore::EntityId& a_outEntityId) const
    {
        a_outEntityId = GameCore::k_invalidEntityId;
        if (m_engine == nullptr || m_engine->active_world() == nullptr)
        {
            return false;
        }

        const DebugCamera::Ray ray =
            m_debugCamera.pick_ray(a_request.normalizedX, a_request.normalizedY);
        GameCore::GameWorld* debugWorld = m_engine->active_world();
        float bestRayDistance = (std::numeric_limits<float>::max)();
        bool hasHit = false;

        auto evaluateHit =
            [&a_outEntityId, &bestRayDistance, &hasHit](
                GameCore::EntityId a_entityId,
                const RayDistance& a_distance,
                float a_radius) noexcept
        {
            if (a_distance.rayDistance >= bestRayDistance)
            {
                return;
            }

            if (a_distance.distanceSq > a_radius * a_radius)
            {
                return;
            }

            bestRayDistance = a_distance.rayDistance;
            a_outEntityId = a_entityId;
            hasHit = true;
        };

        auto pickCamera =
            [&ray, &evaluateHit](
                GameCore::EntityId a_entityId,
                const ECS::TransformComponent& a_transform,
                const ECS::CameraComponent& a_camera) noexcept
        {
            constexpr std::array<uint32_t, 24> k_lineVertexToCorner = {
                0, 1, 1, 2, 2, 3, 3, 0,
                4, 5, 5, 6, 6, 7, 7, 4,
                0, 4, 1, 5, 2, 6, 3, 7,
            };
            constexpr std::array<uint32_t, 6> k_markerLineIndices = {
                0, 1, 1, 2, 2, 0,
            };

            const Math::float4x4 world = Math::make_affine_matrix(
                Math::float3(1.0f, 1.0f, 1.0f),
                a_transform.rotation,
                a_transform.position);
            std::array<Math::float3, 8> corners{};
            for (uint32_t cornerIndex = 0; cornerIndex < 4u; ++cornerIndex)
            {
                corners[cornerIndex] = transform_point(
                    world,
                    make_camera_frustum_corner(
                        cornerIndex,
                        a_camera,
                        k_cameraFrustumNear));
                corners[cornerIndex + 4u] = transform_point(
                    world,
                    make_camera_frustum_corner(
                        cornerIndex,
                        a_camera,
                        k_cameraFrustumFar));
            }

            const float farHalfHeight =
                k_cameraFrustumFar *
                std::tan(
                    std::clamp(a_camera.fovY, 1.0f, 179.0f) *
                    Math::k_pi / 180.0f * 0.5f);
            const float farHalfWidth = farHalfHeight *
                (a_camera.aspectRatio > 0.0f ? a_camera.aspectRatio : 1.0f);
            const float markerHalfWidth =
                (std::min)(farHalfWidth, farHalfHeight) * 0.38f;
            const float markerHeight = farHalfHeight * 0.52f;
            const std::array<Math::float3, 3> marker = {
                transform_point(world,
                    Math::float3(-markerHalfWidth,
                        farHalfHeight,
                        k_cameraFrustumFar)),
                transform_point(world,
                    Math::float3(0.0f,
                        farHalfHeight + markerHeight,
                        k_cameraFrustumFar)),
                transform_point(world,
                    Math::float3(markerHalfWidth,
                        farHalfHeight,
                        k_cameraFrustumFar)),
            };

            for (uint32_t lineIndex = 0;
                lineIndex < k_lineVertexToCorner.size();
                lineIndex += 2u)
            {
                RayDistance distance{};
                if (!distance_ray_segment(
                    ray,
                    corners[k_lineVertexToCorner[lineIndex]],
                    corners[k_lineVertexToCorner[lineIndex + 1u]],
                    distance))
                {
                    continue;
                }

                evaluateHit(
                    a_entityId,
                    distance,
                    debug_pick_radius(distance.rayDistance));
            }

            for (uint32_t lineIndex = 0;
                lineIndex < k_markerLineIndices.size();
                lineIndex += 2u)
            {
                RayDistance distance{};
                if (!distance_ray_segment(
                    ray,
                    marker[k_markerLineIndices[lineIndex]],
                    marker[k_markerLineIndices[lineIndex + 1u]],
                    distance))
                {
                    continue;
                }

                evaluateHit(
                    a_entityId,
                    distance,
                    debug_pick_radius(distance.rayDistance));
            }
        };

        auto pickTransformPoint =
            [&ray, &evaluateHit](
                GameCore::EntityId a_entityId,
                const ECS::TransformComponent& a_transform) noexcept
        {
            RayDistance distance{};
            if (!distance_ray_point(ray, a_transform.position, distance))
            {
                return;
            }

            const float scaleRadius = (std::max)(
                0.25f,
                (std::max)(
                    std::abs(a_transform.scale.x),
                    (std::max)(
                        std::abs(a_transform.scale.y),
                        std::abs(a_transform.scale.z))) *
                    0.35f);
            evaluateHit(
                a_entityId,
                distance,
                (std::max)(scaleRadius, debug_pick_radius(distance.rayDistance)));
        };

        auto pickLight =
            [&ray, &evaluateHit](
                GameCore::EntityId a_entityId,
                const ECS::TransformComponent& a_transform,
                float a_length) noexcept
        {
            const Math::float3 start = a_transform.position;
            const Math::float3 end =
                start + light_forward_axis(a_transform) * a_length;

            RayDistance segmentDistance{};
            if (distance_ray_segment(ray, start, end, segmentDistance))
            {
                evaluateHit(
                    a_entityId,
                    segmentDistance,
                    debug_pick_radius(segmentDistance.rayDistance));
            }

            RayDistance pointDistance{};
            if (distance_ray_point(ray, start, pointDistance))
            {
                evaluateHit(
                    a_entityId,
                    pointDistance,
                    (std::max)(0.25f,
                        debug_pick_radius(pointDistance.rayDistance)));
            }
        };

        auto pickObject =
            [&pickCamera, &pickTransformPoint, &pickLight](
                GameCore::EntityId a_entityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
        {
            ECS::TransformComponent* transform = nullptr;
            if (!a_object.get_component(transform) || transform == nullptr)
            {
                return;
            }

            ECS::CameraComponent* camera = nullptr;
            if (a_object.get_component(camera) && camera != nullptr)
            {
                pickCamera(a_entityId, *transform, *camera);
                return;
            }

            ECS::DirectionalLightComponent* directionalLight = nullptr;
            if (a_object.get_component(directionalLight) &&
                directionalLight != nullptr)
            {
                pickLight(a_entityId, *transform, 3.0f);
                return;
            }

            ECS::PointLightComponent* pointLight = nullptr;
            if (a_object.get_component(pointLight) && pointLight != nullptr)
            {
                pickTransformPoint(a_entityId, *transform);
                return;
            }

            ECS::SpotLightComponent* spotLight = nullptr;
            if (a_object.get_component(spotLight) && spotLight != nullptr)
            {
                pickLight(a_entityId, *transform, 2.5f);
                return;
            }

            ECS::RenderableInfoComponent* renderableInfo = nullptr;
            const bool hasRenderable =
                a_object.get_component(renderableInfo) &&
                renderableInfo != nullptr &&
                renderableInfo->objectId != ECS::k_invalidRenderableId;
            if (!hasRenderable)
            {
                pickTransformPoint(a_entityId, *transform);
            }
        };

        bool hasCollectedSceneObjects = false;
        if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            const Result collectResult =
                debugWorld->for_each_object_in_scene(
                    m_currentSceneId,
                    pickObject);
            hasCollectedSceneObjects = static_cast<bool>(collectResult);
        }
        if (!hasCollectedSceneObjects)
        {
            (void)debugWorld->for_each_object(pickObject);
        }

        return hasHit;
    }

    Core::IO::Path EditorManager::current_asset_drop_folder() const noexcept
    {
        if (m_assetRootPath.is_empty())
        {
            return {};
        }

        if (m_assetBrowser == nullptr)
        {
            return m_assetRootPath;
        }

        Core::IO::Path folderPath =
            m_assetBrowser->current_asset_folder_path().normalize();
        if (folderPath.is_empty())
        {
            return m_assetRootPath;
        }

        const std::string assetRoot = m_assetRootPath.normalize().utf8();
        const std::string folder = folderPath.utf8();
        if (folder == assetRoot || folder.rfind(assetRoot + "/", 0) == 0)
        {
            return folderPath;
        }

        return m_assetRootPath;
    }

    std::string EditorManager::make_asset_relative_name(
        const Core::IO::Path& a_assetPath) const
    {
        const std::string assetRoot = m_assetRootPath.normalize().utf8();
        const std::string assetPath = a_assetPath.normalize().utf8();
        if (!assetRoot.empty() && assetPath.rfind(assetRoot + "/", 0) == 0)
        {
            return assetPath.substr(assetRoot.size() + 1);
        }

        return a_assetPath.filename();
    }

    void EditorManager::sync_debug_selection()
    {
        if (m_engine == nullptr || m_engine->active_world() == nullptr)
        {
            return;
        }

        GameCore::GameWorld* debugWorld = m_engine->active_world();
        GpuData::DebugSelectionGpu selection{};
        uint32_t selectedObjectId = 0;
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
        auto makeSpotShadowFrustumItem =
            [](const ECS::TransformComponent& a_transform,
                const ECS::SpotLightComponent& a_spotLight,
                bool a_isSelected) noexcept
        {
            const float range = (std::max)(a_spotLight.range, 0.001f);
            const float nearClip = std::clamp(
                a_spotLight.shadowNearClip,
                0.001f,
                (std::max)(range - 0.001f, 0.001f));
            const float outerAngle = std::clamp(
                a_spotLight.outerAngleDegrees,
                1.0f,
                89.0f);

            GpuData::DebugSelectionItemGpu item{};
            item.world =
                Math::y_axis_matrix(Math::k_pi) *
                Math::xyz_rotate_matrix(a_transform.rotation) *
                Math::translate_matrix(a_transform.position);
            item.color = a_isSelected
                ? Math::float4(1.0f, 0.84f, 0.18f, 1.0f)
                : Math::float4(0.2f, 0.95f, 1.0f, 1.0f);
            item.camera = Math::float4(
                outerAngle * 2.0f,
                1.0f,
                nearClip,
                range);
            item.shape = static_cast<uint32_t>(
                GpuData::DebugSelectionShape::CameraFrustum);
            item.isEnabled = 1;
            return item;
        };
        auto makeLightLineItem =
            [](const ECS::TransformComponent& a_transform,
                const Math::float3& a_end,
                const Math::float4& a_color) noexcept
        {
            GpuData::DebugSelectionItemGpu item{};
            item.world = Math::make_affine_matrix(
                Math::float3(1.0f, 1.0f, 1.0f),
                a_transform.rotation,
                a_transform.position);
            item.color = a_color;
            item.camera = Math::float4(a_end.x, a_end.y, a_end.z, 0.0f);
            item.shape =
                static_cast<uint32_t>(GpuData::DebugSelectionShape::Line);
            item.isEnabled = 1;
            return item;
        };
        auto appendLightArrow =
            [&appendDebugItem](
                const ECS::TransformComponent& a_transform,
                float a_length,
                const Math::float4& a_color)
        {
            GpuData::DebugSelectionItemGpu item{};
            item.world = Math::make_affine_matrix(
                Math::float3(1.0f, 1.0f, 1.0f),
                a_transform.rotation,
                a_transform.position);
            item.color = a_color;
            item.camera = Math::float4(
                a_length,
                a_length * 0.26f,
                a_length * 0.14f,
                0.0f);
            item.shape = static_cast<uint32_t>(
                GpuData::DebugSelectionShape::LightArrow);
            item.isEnabled = 1;
            appendDebugItem(item);
        };
        auto appendPointLightMarker =
            [&appendDebugItem, &makeLightLineItem](
                const ECS::TransformComponent& a_transform,
                float a_radius,
                const Math::float4& a_color)
        {
            ECS::TransformComponent markerTransform = a_transform;
            markerTransform.rotation = Math::float3::zero();
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(a_radius, 0.0f, 0.0f),
                a_color));
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(-a_radius, 0.0f, 0.0f),
                a_color));
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(0.0f, a_radius, 0.0f),
                a_color));
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(0.0f, -a_radius, 0.0f),
                a_color));
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(0.0f, 0.0f, a_radius),
                a_color));
            appendDebugItem(makeLightLineItem(
                markerTransform,
                Math::float3(0.0f, 0.0f, -a_radius),
                a_color));
        };
        if (m_selectedEntityId != GameCore::k_invalidEntityId)
        {
            const ECS::RenderableInfoComponent* renderableInfo = nullptr;
            const bool hasRenderableOutline =
                debugWorld->get_component<ECS::RenderableInfoComponent>(
                    m_selectedEntityId, renderableInfo) &&
                renderableInfo != nullptr &&
                renderableInfo->objectId != ECS::k_invalidRenderableId;

            const ECS::TransformComponent* transform = nullptr;
            const Result transformResult =
                debugWorld->get_component<ECS::TransformComponent>(
                    m_selectedEntityId, transform);
            const ECS::CameraComponent* camera = nullptr;
            if (transformResult)
            {
                (void)debugWorld->get_component<ECS::CameraComponent>(
                    m_selectedEntityId,
                    camera);
                const ECS::DirectionalLightComponent* directionalLight = nullptr;
                const ECS::PointLightComponent* pointLight = nullptr;
                const ECS::SpotLightComponent* spotLight = nullptr;
                const bool hasLight =
                    debugWorld->get_component<ECS::DirectionalLightComponent>(
                        m_selectedEntityId, directionalLight) &&
                        directionalLight != nullptr ||
                    debugWorld->get_component<ECS::PointLightComponent>(
                        m_selectedEntityId, pointLight) &&
                        pointLight != nullptr ||
                    debugWorld->get_component<ECS::SpotLightComponent>(
                        m_selectedEntityId, spotLight) &&
                        spotLight != nullptr;
                if (camera == nullptr && !hasRenderableOutline && !hasLight)
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

            if (hasRenderableOutline)
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
        auto appendLightObject =
            [this,
                &appendDebugItem,
                &appendLightArrow,
                &appendPointLightMarker,
                &makeSpotShadowFrustumItem](
                GameCore::EntityId a_entityId,
                GameCore::SceneId,
                GameCore::GameObject& a_object)
        {
            ECS::TransformComponent* transform = nullptr;
            if (!a_object.get_component(transform) || transform == nullptr)
            {
                return;
            }

            const bool isSelected = a_entityId == m_selectedEntityId;
            const Math::float4 selectedColor =
                Math::float4(1.0f, 0.84f, 0.18f, 1.0f);

            ECS::DirectionalLightComponent* directionalLight = nullptr;
            if (a_object.get_component(directionalLight) &&
                directionalLight != nullptr)
            {
                appendLightArrow(
                    *transform,
                    3.0f,
                    isSelected
                        ? selectedColor
                        : Math::float4(1.0f, 0.92f, 0.25f, 1.0f));
                return;
            }

            ECS::PointLightComponent* pointLight = nullptr;
            if (a_object.get_component(pointLight) && pointLight != nullptr)
            {
                appendPointLightMarker(
                    *transform,
                    0.45f,
                    isSelected
                        ? selectedColor
                        : Math::float4(1.0f, 0.72f, 0.32f, 1.0f));
                return;
            }

            ECS::SpotLightComponent* spotLight = nullptr;
            if (a_object.get_component(spotLight) && spotLight != nullptr)
            {
                if (spotLight->castsShadow)
                {
                    appendDebugItem(makeSpotShadowFrustumItem(
                        *transform,
                        *spotLight,
                        isSelected));
                }
                appendLightArrow(
                    *transform,
                    2.5f,
                    isSelected
                        ? selectedColor
                        : Math::float4(0.52f, 0.82f, 1.0f, 1.0f));
            }
        };
        bool hasCollectedSceneCameras = false;
        if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            const Result collectResult =
                debugWorld->for_each_object_in_scene(
                m_currentSceneId,
                appendCameraObject);
            hasCollectedSceneCameras = static_cast<bool>(collectResult);
        }
        if (!hasCollectedSceneCameras)
        {
            (void)debugWorld->for_each_object(appendCameraObject);
        }

        bool hasCollectedSceneLights = false;
        if (m_currentSceneId != GameCore::k_invalidSceneId)
        {
            const Result collectResult =
                debugWorld->for_each_object_in_scene(
                m_currentSceneId,
                appendLightObject);
            hasCollectedSceneLights = static_cast<bool>(collectResult);
        }
        if (!hasCollectedSceneLights)
        {
            (void)debugWorld->for_each_object(appendLightObject);
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
        update_background_scene_reload();
        process_pending_script_action();
        update_auto_script_build();
        const Result dropResult = handle_dropped_asset_files();
        if (!dropResult)
        {
            log_result("Failed to import dropped asset files", dropResult);
            set_status_message(
                std::string("ドロップファイルのインポートに失敗しました: ") +
                    std::string(dropResult.message),
                true);
        }
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
            const bool canUndo = m_bridge != nullptr && m_bridge->can_undo();
            const bool canRedo = m_bridge != nullptr && m_bridge->can_redo();
            const ImVec2 editButtonSize(
                ImGui::GetFrameHeight(),
                ImGui::GetFrameHeight());

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::BeginDisabled(!canUndo);
            if (ImGui::Button(CUE_ICON_UNDO "##MenuUndo", editButtonSize))
            {
                undo_last_command();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Undo (Ctrl+Z)");
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(!canRedo);
            if (ImGui::Button(CUE_ICON_REDO "##MenuRedo", editButtonSize))
            {
                redo_last_command();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Redo (Ctrl+Y)");
            }
            ImGui::PopStyleVar(2);

            ImGui::SameLine();
            const bool isFileMenuOpen = ImGui::BeginMenu("ファイル");
            const bool isFileMenuLabelHovered =
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            if (isFileMenuOpen)
            {
                const bool canOperateScene =
                    m_currentSceneId != GameCore::k_invalidSceneId &&
                    !m_currentScenePath.empty() &&
                    !m_isScriptActionActive &&
                    m_sceneReloadOperation == nullptr;

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
                    const Result result = start_background_scene_reload();
                    if (!result)
                    {
                        log_result("Failed to reload scene", result);
                        set_status_message("シーン再読み込みに失敗しました。", true);
                    }
                }

                if (should_close_menu_on_hover_leave(isFileMenuLabelHovered))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("表示"))
            {
                draw_display_menu_items();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("ナビゲーション"))
            {
                const bool canBakeNavigation =
                    m_engine != nullptr && !m_projectPath.empty() &&
                    m_currentSceneId != GameCore::k_invalidSceneId &&
                    !m_isScriptActionActive && !m_engine->is_playing();

                if (ImGui::MenuItem(
                    "Scene NavMesh を Bake", nullptr, false, canBakeNavigation))
                {
                    const Result result = bake_current_scene_navigation();
                    if (!result)
                    {
                        log_result("Failed to bake navigation", result);
                        set_status_message("NavMesh Bake に失敗しました。", true);
                    }
                }

                ImGui::MenuItem(
                    "Debug Window",
                    nullptr,
                    &m_showNavigationDebugWindow,
                    m_engine != nullptr);

                ImGui::EndMenu();
            }

            draw_skybox_menu();

            if (ImGui::BeginMenu("ビルド"))
            {
                const bool canEditBuildSettings = !m_isScriptActionActive;
                const bool canBuildGameRelease =
                    m_buildSystem != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;

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
                        "ゲーム配布アプリ設定", nullptr, false,
                        canBuildGameRelease))
                {
                    const Result result =
                        load_game_release_app_settings_to_buffers();
                    if (!result)
                    {
                        log_result(
                            "Failed to load game release app settings",
                            result);
                        set_status_message(
                            "ゲーム配布アプリ設定の読み込みに失敗しました。",
                            true);
                    }
                    else
                    {
                        m_openGameReleaseAppSettingsPopup = true;
                    }
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
            ImGui::SameLine();
            draw_play_controls();
            ImGui::SameLine();
            draw_script_build_configuration_combo();
            ImGui::SameLine();

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

            ImGui::EndMenuBar();
        }
        menuBarTimer.stop();
        m_currentUpdateMetrics.menuBarMs =
            menuBarTimer.elapsed_ticks().ms_f64();

        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        constexpr float k_statusBarHeight = 24.0f;
        const ImVec2 dockspaceSize(
            0.0f,
            (std::max)(0.0f,
                ImGui::GetContentRegionAvail().y - k_statusBarHeight));
        ImGui::DockSpace(dockspace_id, dockspaceSize, ImGuiDockNodeFlags_None);
        draw_status_bar();
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
        draw_navigation_debug_window();
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
            if (m_assetBrowser->was_asset_selected() &&
                to_lower_ascii(m_selectedAssetPath.extension()) == ".cuematerial")
            {
                m_selectedEntityId = GameCore::k_invalidEntityId;
                m_selectedSceneId = GameCore::k_invalidSceneId;
            }
            assetBrowserTimer.stop();
            m_currentUpdateMetrics.assetBrowserMs =
                assetBrowserTimer.elapsed_ticks().ms_f64();
        }

        Core::Time::Timer createScriptPopupTimer(m_platform->clock());
        createScriptPopupTimer.start();
        draw_create_script_popup();
        draw_game_release_app_settings_popup();
        draw_background_progress_window();
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
        const GameCore::EntityId selectedEntityBeforeHierarchy =
            m_selectedEntityId;
        const GameCore::SceneId selectedSceneBeforeHierarchy =
            m_selectedSceneId;
        if (m_hierarchy != nullptr)
        {
            m_hierarchy->set_scenes(collect_hierarchy_scenes());
        }
        m_hierarchy->update();
        if (m_selectedEntityId != selectedEntityBeforeHierarchy &&
            m_selectedEntityId != GameCore::k_invalidEntityId)
        {
            m_selectedAssetPath = {};
        }
        if (m_selectedSceneId != selectedSceneBeforeHierarchy &&
            m_selectedSceneId != GameCore::k_invalidSceneId)
        {
            m_selectedAssetPath = {};
        }
        hierarchyTimer.stop();
        m_currentUpdateMetrics.hierarchyMs =
            hierarchyTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer inspectorTimer(m_platform->clock());
        inspectorTimer.start();
        m_inspector->update();
        inspectorTimer.stop();
        m_currentUpdateMetrics.inspectorMs =
            inspectorTimer.elapsed_ticks().ms_f64();

        focus_pending_window();

        updateTimer.stop();
        m_currentUpdateMetrics.totalMs =
            updateTimer.elapsed_ticks().ms_f64();
        m_lastUpdateMetrics = m_currentUpdateMetrics;
    }
}
