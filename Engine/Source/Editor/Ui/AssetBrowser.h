// AssetBrowser の役割と公開要素を定義する

#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Asset/AssetManager.h>

// === Editor asset includes ===
#include <TextureCooker.h>

// === Editor includes ===
#include "AssetDragDrop.h"
#include "Icon.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class AssetBrowser final
    {
    public:
        explicit AssetBrowser(Core::IO::IFileSystem* a_fileSystem)
            : m_fileSystem(a_fileSystem)
        {
        }
        ~AssetBrowser() = default;

        void set_texture_preview_dependencies(
            AssetManager* a_assetManager,
            RHI::DX12::D3D12Backend* a_backend) noexcept
        {
            m_assetManager = a_assetManager;
            m_backend = a_backend;
            clear_texture_preview_cache();
        }

        void set_asset_root_path(const Core::IO::Path& a_assetRootPath)
        {
            m_assetRootPath = a_assetRootPath.normalize();
            m_selectedFolderPath = m_assetRootPath;
            clear_cache();
        }

        void clear_asset_root_path()
        {
            m_assetRootPath = {};
            m_selectedFolderPath = {};
            clear_cache();
        }

        void set_selected_asset_path(Core::IO::Path* a_selectedAssetPath) noexcept
        {
            m_selectedAssetPath = a_selectedAssetPath;
        }

        [[nodiscard]] bool was_asset_selected() const noexcept
        {
            return m_wasAssetSelected;
        }

        void refresh()
        {
            clear_cache();
            clear_texture_preview_cache();
        }

        [[nodiscard]] Core::IO::Path current_asset_folder_path() const noexcept
        {
            const Core::IO::Path assetRootPath = m_assetRootPath.normalize();
            if (assetRootPath.is_empty())
            {
                return {};
            }

            const Core::IO::Path selectedFolderPath =
                m_selectedFolderPath.normalize();
            if (selectedFolderPath.is_empty())
            {
                return assetRootPath;
            }

            const std::string assetRoot = assetRootPath.utf8();
            const std::string selectedFolder = selectedFolderPath.utf8();
            if (selectedFolder == assetRoot ||
                selectedFolder.rfind(assetRoot + "/", 0) == 0)
            {
                return selectedFolderPath;
            }

            return assetRootPath;
        }

        void update()
        {
            m_wasAssetSelected = false;
            m_shouldOpenMakeCubeTexturePopup = false;

            ImGui::Begin("Asset Browser");

            if (m_fileSystem == nullptr)
            {
                ImGui::TextUnformatted("FileSystem が初期化されていません。");
                ImGui::End();
                return;
            }

            if (m_assetRootPath.is_empty())
            {
                ImGui::TextUnformatted("プロジェクトの Assets フォルダが未設定です。");
                ImGui::End();
                return;
            }

            bool rootExists = false;
            Result existsResult =
                m_fileSystem->exists(m_assetRootPath, &rootExists);
            if (!existsResult || !rootExists)
            {
                ImGui::TextUnformatted("Assets フォルダが見つかりません。");
                ImGui::End();
                return;
            }

            if (m_selectedFolderPath.is_empty())
            {
                m_selectedFolderPath = m_assetRootPath;
            }

            if (ImGui::Button("Refresh"))
            {
                clear_cache();
            }

            ImGui::BeginChild("AssetFolderTree", ImVec2(260.0f, 0.0f), true);
            draw_folder_node(m_assetRootPath);
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("AssetFileList", ImVec2(0.0f, 0.0f), true);
            draw_file_list();
            ImGui::EndChild();

            if (m_shouldOpenMakeCubeTexturePopup)
            {
                ImGui::OpenPopup("Make Cube Texture");
            }

            draw_make_cube_texture_popup();
            ImGui::End();
        }

    private:
        inline static constexpr std::array<const char*, 6> k_cubeFaceLabels{
            "+X / Right",
            "-X / Left",
            "+Y / Up",
            "-Y / Down",
            "+Z / Front",
            "-Z / Back"
        };

        void clear_cache()
        {
            m_folderCache.clear();
            m_fileCache.clear();
        }

        void clear_texture_preview_cache()
        {
            RHI::IViewManager* viewManager =
                m_backend != nullptr ? m_backend->get_view_manager() : nullptr;
            if (viewManager != nullptr)
            {
                for (const auto& [_, preview] : m_texturePreviewCache)
                {
                    if (preview.srvHandle.valid())
                    {
                        (void)viewManager->destroy_view(preview.srvHandle);
                    }
                }
            }

            m_texturePreviewCache.clear();
        }

        void draw_folder_node(const Core::IO::Path& a_folderPath)
        {
            const std::string normalizedPath = a_folderPath.normalize().utf8();
            const bool isRoot = normalizedPath == m_assetRootPath.utf8();
            const bool isSelected =
                normalizedPath == m_selectedFolderPath.normalize().utf8();
            const std::vector<Core::IO::Path>& childFolders =
                collect_child_folders(a_folderPath);

            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSelected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (childFolders.empty())
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (isRoot)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            const std::string label = isRoot
                ? std::string("Assets")
                : a_folderPath.filename();

            ImGui::PushID(normalizedPath.c_str());
            const bool isOpen = ImGui::TreeNodeEx(
                label.c_str(), flags);

            if (ImGui::IsItemClicked())
            {
                m_selectedFolderPath = a_folderPath.normalize();
            }

            if (isOpen)
            {
                for (const Core::IO::Path& childFolder : childFolders)
                {
                    draw_folder_node(childFolder);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        void draw_file_list()
        {
            ImGui::Text("Folder: %s", display_folder_name(
                m_selectedFolderPath).c_str());
            ImGui::Separator();

            draw_file_list_context_menu();

            const std::vector<Core::IO::Path>& files =
                collect_child_files(m_selectedFolderPath);
            if (files.empty())
            {
                ImGui::TextUnformatted("ファイルはありません。");
                return;
            }

            m_fileDrawIndex = 0;
            for (const Core::IO::Path& filePath : files)
            {
                draw_file_button(filePath);
            }
        }

        void draw_file_list_context_menu()
        {
            if (!ImGui::BeginPopupContextWindow(
                    "AssetFileListContext",
                    ImGuiPopupFlags_MouseButtonRight))
            {
                return;
            }

            if (ImGui::MenuItem("Make Cube Texture"))
            {
                begin_make_cube_texture();
            }

            ImGui::EndPopup();
        }

        void begin_make_cube_texture()
        {
            m_cubeTextureDestinationFolder = current_asset_folder_path();
            m_cubeTextureFacePaths = {};
            m_cubeTextureStatusMessage.clear();
            m_cubeTextureStatusIsError = false;

            constexpr char k_defaultName[] = "CubeTexture.dds";
            std::fill(
                std::begin(m_cubeTextureOutputName),
                std::end(m_cubeTextureOutputName),
                '\0');
            std::copy(
                std::begin(k_defaultName),
                std::end(k_defaultName),
                std::begin(m_cubeTextureOutputName));
            m_shouldOpenMakeCubeTexturePopup = true;
        }

        void draw_file_button(const Core::IO::Path& a_filePath)
        {
            constexpr float k_tileWidth = 96.0f;
            constexpr float k_buttonSize = 64.0f;
            constexpr float k_tileSpacing = 12.0f;

            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const int columnCount = (std::max)(
                1,
                static_cast<int>(availableWidth / (k_tileWidth + k_tileSpacing)));
            const int columnIndex = m_fileDrawIndex % columnCount;

            ImGui::PushID(a_filePath.utf8().c_str());
            ImGui::BeginGroup();

            const float tileStartX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(
                tileStartX + (k_tileWidth - k_buttonSize) * 0.5f);
            const bool isPressed = ImGui::InvisibleButton(
                "##FileButton",
                ImVec2(k_buttonSize, k_buttonSize));

            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            const bool isMaterial = is_material_file(a_filePath);
            const bool isEffect = is_effect_file(a_filePath);
            if (isPressed && (isMaterial || isEffect) &&
                m_selectedAssetPath != nullptr)
            {
                *m_selectedAssetPath = a_filePath.normalize();
                m_wasAssetSelected = true;
            }
            const bool isHovered = ImGui::IsItemHovered();
            const bool isActive = ImGui::IsItemActive();
            const ImU32 buttonColor = ImGui::GetColorU32(
                isActive ? ImGuiCol_ButtonActive
                         : (isHovered ? ImGuiCol_ButtonHovered
                                      : ImGuiCol_Button));
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                buttonMin,
                buttonMax,
                buttonColor,
                ImGui::GetStyle().FrameRounding);
            drawList->AddRect(
                buttonMin,
                buttonMax,
                ImGui::GetColorU32(ImGuiCol_Border),
                ImGui::GetStyle().FrameRounding);

            const TexturePreview* texturePreview =
                get_texture_preview(a_filePath);
            if (texturePreview != nullptr && texturePreview->isReady)
            {
                draw_texture_preview(*texturePreview, buttonMin, buttonMax);
            }
            else
            {
                constexpr float k_iconFontSize = k_buttonSize;
                const char* icon = isMaterial
                    ? CUE_ICON_MATERIAL
                    : (isEffect ? CUE_ICON_SHADER
                                : (is_texture_file(a_filePath)
                                          ? CUE_ICON_IMAGE
                                          : CUE_ICON_Unknown));
                ImFont* font = ImGui::GetFont();
                const ImVec2 iconSize = font->CalcTextSizeA(
                    k_iconFontSize,
                    FLT_MAX,
                    0.0f,
                    icon);
                const ImVec2 iconPos(
                    buttonMin.x + (k_buttonSize - iconSize.x) * 0.5f,
                    buttonMin.y + (k_buttonSize - iconSize.y) * 0.5f);
                drawList->AddText(
                    font,
                    k_iconFontSize,
                    iconPos,
                    ImGui::GetColorU32(ImGuiCol_Text),
                    icon);
            }

            if (isMaterial && ImGui::BeginDragDropSource())
            {
                const std::string payloadPath = a_filePath.normalize().utf8();
                ImGui::SetDragDropPayload(
                    k_materialAssetPayloadType,
                    payloadPath.c_str(),
                    payloadPath.size() + 1);
                ImGui::Text("%s %s", CUE_ICON_MATERIAL,
                    a_filePath.filename().c_str());
                ImGui::EndDragDropSource();
            }
            else if (is_texture_file(a_filePath) &&
                ImGui::BeginDragDropSource())
            {
                Core::IO::Path texturePath{};
                if (resolve_texture_preview_path(a_filePath, texturePath))
                {
                    const std::string payloadPath =
                        texturePath.normalize().utf8();
                    ImGui::SetDragDropPayload(
                        k_textureAssetPayloadType,
                        payloadPath.c_str(),
                        payloadPath.size() + 1);
                    ImGui::Text("%s %s", CUE_ICON_IMAGE,
                        texturePath.filename().c_str());
                }
                else
                {
                    ImGui::TextUnformatted(
                        "Cooked texture が見つかりません。");
                }
                ImGui::EndDragDropSource();
            }

            const std::string filename = a_filePath.filename();
            ImGui::PushTextWrapPos(tileStartX + k_tileWidth);
            ImGui::SetCursorPosX(tileStartX);
            ImGui::TextUnformatted(filename.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndGroup();
            ImGui::PopID();

            ++m_fileDrawIndex;
            if (columnIndex + 1 < columnCount)
            {
                ImGui::SameLine(0.0f, k_tileSpacing);
            }
        }

        void draw_make_cube_texture_popup()
        {
            bool isOpen = true;
            if (!ImGui::BeginPopupModal(
                    "Make Cube Texture",
                    &isOpen,
                    ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            ImGui::Text("Output folder: %s",
                display_folder_name(m_cubeTextureDestinationFolder).c_str());
            ImGui::InputText(
                "Output",
                m_cubeTextureOutputName,
                sizeof(m_cubeTextureOutputName));
            ImGui::Separator();
            ImGui::TextUnformatted(
                "Face order: +X, -X, +Y, -Y, +Z, -Z. All images must use the same resolution.");

            const std::vector<Core::IO::Path> candidates =
                collect_cube_face_candidates(m_cubeTextureDestinationFolder);
            const bool hasCandidates = !candidates.empty();
            if (!hasCandidates)
            {
                ImGui::TextDisabled(
                    "このフォルダに CubeMap 面として使える画像がありません。");
            }

            ImGui::BeginDisabled(!hasCandidates);
            for (size_t faceIndex = 0; faceIndex < m_cubeTextureFacePaths.size();
                 ++faceIndex)
            {
                draw_cube_face_selector(faceIndex, candidates);
            }
            ImGui::EndDisabled();

            if (!m_cubeTextureStatusMessage.empty())
            {
                const ImVec4 color = m_cubeTextureStatusIsError
                    ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                    : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
                ImGui::TextColored(color, "%s",
                    m_cubeTextureStatusMessage.c_str());
            }

            ImGui::Separator();
            const bool canCreate = hasCandidates && are_cube_faces_selected() &&
                m_cubeTextureOutputName[0] != '\0';
            ImGui::BeginDisabled(!canCreate);
            if (ImGui::Button("Create"))
            {
                const Result result = create_cube_texture_from_selection();
                if (result)
                {
                    m_cubeTextureStatusMessage = "Cube texture を作成しました。";
                    m_cubeTextureStatusIsError = false;
                    clear_cache();
                    clear_texture_preview_cache();
                }
                else
                {
                    m_cubeTextureStatusMessage =
                        "Cube texture の作成に失敗しました。";
                    m_cubeTextureStatusIsError = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void draw_cube_face_selector(
            size_t a_faceIndex,
            const std::vector<Core::IO::Path>& a_candidates)
        {
            const Core::IO::Path& selectedPath =
                m_cubeTextureFacePaths[a_faceIndex];
            const std::string preview = selectedPath.is_empty()
                ? std::string("<未設定>")
                : selectedPath.filename();
            if (!ImGui::BeginCombo(
                    k_cubeFaceLabels[a_faceIndex],
                    preview.c_str()))
            {
                return;
            }

            for (const Core::IO::Path& candidate : a_candidates)
            {
                const bool isSelected =
                    candidate.normalize().utf8() ==
                    selectedPath.normalize().utf8();
                if (ImGui::Selectable(candidate.filename().c_str(), isSelected))
                {
                    m_cubeTextureFacePaths[a_faceIndex] = candidate.normalize();
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        [[nodiscard]] std::vector<Core::IO::Path> collect_cube_face_candidates(
            const Core::IO::Path& a_folderPath)
        {
            std::vector<Core::IO::Path> candidates{};
            const std::vector<Core::IO::Path>& files =
                collect_child_files(a_folderPath);
            for (const Core::IO::Path& filePath : files)
            {
                if (is_cube_face_source_file(filePath))
                {
                    candidates.push_back(filePath.normalize());
                }
            }
            return candidates;
        }

        [[nodiscard]] bool are_cube_faces_selected() const noexcept
        {
            return std::all_of(
                m_cubeTextureFacePaths.begin(),
                m_cubeTextureFacePaths.end(),
                [](const Core::IO::Path& a_path)
                {
                    return !a_path.is_empty();
                });
        }

        [[nodiscard]] Core::IO::Path cube_texture_output_path() const
        {
            std::string outputName(m_cubeTextureOutputName);
            if (outputName.empty())
            {
                return {};
            }

            Core::IO::Path outputPath(outputName);
            if (outputPath.extension().empty())
            {
                outputName += ".dds";
                outputPath = Core::IO::Path(outputName);
            }

            return Core::IO::Path::join(
                m_cubeTextureDestinationFolder,
                Core::IO::Path(outputPath.filename()));
        }

        [[nodiscard]] Result create_cube_texture_from_selection()
        {
            if (m_fileSystem == nullptr)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "FileSystem is not initialized.");
            }

            const Core::IO::Path outputPath = cube_texture_output_path();
            return TextureCooker::make_cube_dds_from_faces(
                *m_fileSystem,
                m_cubeTextureFacePaths,
                outputPath);
        }

        [[nodiscard]] const std::vector<Core::IO::Path>& collect_child_folders(
            const Core::IO::Path& a_folderPath)
        {
            const std::string cacheKey = a_folderPath.normalize().utf8();
            if (const auto cacheIt = m_folderCache.find(cacheKey);
                cacheIt != m_folderCache.end())
            {
                return cacheIt->second;
            }

            std::vector<Core::IO::Path> folders{};
            std::vector<Core::IO::Path> entries{};
            const Result result = m_fileSystem->list_directory(
                a_folderPath, &entries);
            if (!result)
            {
                auto [it, _] = m_folderCache.emplace(cacheKey, std::move(folders));
                return it->second;
            }

            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                if (!m_fileSystem->stat(entryPath, &stat) ||
                    stat.type != Core::IO::FileType::directory)
                {
                    continue;
                }

                folders.push_back(entryPath.normalize());
            }

            std::sort(folders.begin(), folders.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.filename() < a_right.filename();
                });
            auto [it, _] = m_folderCache.emplace(cacheKey, std::move(folders));
            return it->second;
        }

        [[nodiscard]] const std::vector<Core::IO::Path>& collect_child_files(
            const Core::IO::Path& a_folderPath)
        {
            const std::string cacheKey = a_folderPath.normalize().utf8();
            if (const auto cacheIt = m_fileCache.find(cacheKey);
                cacheIt != m_fileCache.end())
            {
                return cacheIt->second;
            }

            std::vector<Core::IO::Path> files{};
            std::vector<Core::IO::Path> entries{};
            const Result result = m_fileSystem->list_directory(
                a_folderPath, &entries);
            if (!result)
            {
                auto [it, _] = m_fileCache.emplace(cacheKey, std::move(files));
                return it->second;
            }

            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                if (!m_fileSystem->stat(entryPath, &stat) ||
                    stat.type != Core::IO::FileType::regular)
                {
                    continue;
                }

                files.push_back(entryPath.normalize());
            }

            std::sort(files.begin(), files.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.filename() < a_right.filename();
                });
            auto [it, _] = m_fileCache.emplace(cacheKey, std::move(files));
            return it->second;
        }

        [[nodiscard]] std::string display_folder_name(
            const Core::IO::Path& a_folderPath) const
        {
            const std::string folderPath = a_folderPath.normalize().utf8();
            const std::string assetRootPath = m_assetRootPath.utf8();
            if (folderPath == assetRootPath)
            {
                return "Assets";
            }

            if (folderPath.rfind(assetRootPath + "/", 0) == 0)
            {
                return "Assets/" +
                    folderPath.substr(assetRootPath.size() + 1);
            }

            return a_folderPath.filename();
        }

        [[nodiscard]] static bool is_material_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            return to_lower_ascii(a_filePath.extension()) == ".cuematerial";
        }

        [[nodiscard]] static bool is_effect_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            return to_lower_ascii(a_filePath.extension()) == ".cuefx";
        }

        [[nodiscard]] static bool is_texture_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            const std::string extension = to_lower_ascii(a_filePath.extension());
            return extension == ".cuetexture" || extension == ".png" ||
                extension == ".jpg" || extension == ".jpeg" ||
                extension == ".bmp" || extension == ".tga" ||
                extension == ".dds";
        }

        [[nodiscard]] static bool is_cube_face_source_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            const std::string extension = to_lower_ascii(a_filePath.extension());
            return extension == ".png" ||
                extension == ".jpg" ||
                extension == ".jpeg" ||
                extension == ".bmp" ||
                extension == ".tga" ||
                extension == ".hdr" ||
                extension == ".dds";
        }

        [[nodiscard]] static std::string to_lower_ascii(
            std::string a_text) noexcept
        {
            for (char& character : a_text)
            {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            return a_text;
        }

        [[nodiscard]] bool resolve_texture_preview_path(
            const Core::IO::Path& a_filePath,
            Core::IO::Path& outTexturePath) const
        {
            const std::string extension = to_lower_ascii(a_filePath.extension());
            if (extension == ".cuetexture" || extension == ".dds")
            {
                outTexturePath = a_filePath.normalize();
                return true;
            }

            if (extension != ".png" || m_fileSystem == nullptr)
            {
                return false;
            }

            const Core::IO::Path cookedPath = Core::IO::Path::join(
                a_filePath.parent(),
                Core::IO::Path(a_filePath.stem() + ".dds"));
            bool exists = false;
            const Result result = m_fileSystem->exists(cookedPath, &exists);
            if (!result || !exists)
            {
                return false;
            }

            outTexturePath = cookedPath.normalize();
            return true;
        }

        [[nodiscard]] std::string make_asset_relative_name(
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

        struct TexturePreviewHeader final
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            RHI::ColorFormat format = RHI::ColorFormat::R8G8B8A8_UNORM;
        };

        struct TexturePreview final
        {
            RHI::ViewHandle srvHandle{};
            uint32_t width = 0;
            uint32_t height = 0;
            bool isReady = false;
            bool hasFailed = false;
        };

        [[nodiscard]] Result read_texture_preview_header(
            const Core::IO::Path& a_texturePath,
            TexturePreviewHeader& outHeader) const
        {
            if (m_fileSystem == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "FileSystem is not initialized.");
            }

            Core::IO::FileOpenDesc openDesc{};
            openDesc.access = Core::IO::OpenAccess::read;
            openDesc.create = Core::IO::OpenCreate::open_existing;
            openDesc.flags = Core::IO::OpenFlags::sequential;

            std::unique_ptr<Core::IO::IFile> file = nullptr;
            Result result = m_fileSystem->open(a_texturePath, openDesc, &file);
            if (!result)
            {
                return result;
            }

            std::array<std::byte, sizeof(CueTextureHeader)> data{};
            uint64_t readSize = 0;
            result = file->read(
                std::span<std::byte>(data.data(), data.size()),
                &readSize);
            (void)file->close();
            if (!result)
            {
                return result;
            }
            if (readSize != data.size())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Cooked texture header is incomplete.");
            }

            CueTextureHeader header{};
            std::memcpy(&header, data.data(), sizeof(CueTextureHeader));
            if (header.magic != k_cueTextureMagic ||
                header.version != k_cueTextureVersion ||
                header.width == 0 ||
                header.height == 0 ||
                header.mipCount == 0 ||
                header.arraySize != 1)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Cooked texture header is invalid.");
            }

            outHeader.width = header.width;
            outHeader.height = header.height;
            outHeader.mipLevels = header.mipCount;
            outHeader.format = static_cast<RHI::ColorFormat>(header.format);
            return Result::ok();
        }

        [[nodiscard]] Result load_texture_preview(
            const Core::IO::Path& a_texturePath,
            TexturePreview& outPreview)
        {
            if (m_fileSystem == nullptr || m_assetManager == nullptr ||
                m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Texture preview dependencies are not initialized.");
            }

            uint32_t textureId = AssetManager::k_errorTextureId;
            Result result = m_assetManager->register_texture_from_file(
                *m_fileSystem,
                make_asset_relative_name(a_texturePath),
                a_texturePath,
                textureId);
            if (!result)
            {
                return result;
            }

            RHI::TextureHandle textureHandle{};
            result = m_assetManager->get_texture_handle(textureId, textureHandle);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc textureDesc{};
            result = m_backend->get_texture_manager()->get_texture_desc(
                textureHandle,
                textureDesc);
            if (!result)
            {
                return result;
            }
            if (textureDesc.type != RHI::TextureType::Texture2D)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Only 2D texture preview is supported.");
            }

            RHI::ViewDesc viewDesc{};
            viewDesc.name = "AssetBrowserPreview/" +
                make_asset_relative_name(a_texturePath);
            viewDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            viewDesc.bufferKind = RHI::BufferKind::Texture;
            viewDesc.textureHandle = textureHandle;
            viewDesc.colorFormat = textureDesc.format;
            viewDesc.mipSlice = 0;
            viewDesc.mipLevels = textureDesc.mipLevels;

            RHI::ViewHandle srvHandle{};
            result = m_backend->get_view_manager()->create_view(viewDesc, srvHandle);
            if (!result)
            {
                return result;
            }

            outPreview.srvHandle = srvHandle;
            outPreview.width = textureDesc.width;
            outPreview.height = textureDesc.height;
            outPreview.isReady = true;
            outPreview.hasFailed = false;
            return Result::ok();
        }

        [[nodiscard]] TexturePreview* get_texture_preview(
            const Core::IO::Path& a_filePath)
        {
            Core::IO::Path texturePath{};
            if (!resolve_texture_preview_path(a_filePath, texturePath))
            {
                return nullptr;
            }

            const std::string cacheKey = texturePath.normalize().utf8();
            if (auto cacheIt = m_texturePreviewCache.find(cacheKey);
                cacheIt != m_texturePreviewCache.end())
            {
                return &cacheIt->second;
            }

            TexturePreview preview{};
            const Result result = load_texture_preview(texturePath, preview);
            if (!result)
            {
                preview.isReady = false;
                preview.hasFailed = true;
            }

            auto [it, _] = m_texturePreviewCache.emplace(cacheKey, preview);
            return &it->second;
        }

        void draw_texture_preview(
            const TexturePreview& a_preview,
            const ImVec2& a_buttonMin,
            const ImVec2& a_buttonMax)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    a_preview.srvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            if (srvGpuDescHandle.ptr == 0 ||
                a_preview.width == 0 ||
                a_preview.height == 0)
            {
                return;
            }

            const float buttonWidth = a_buttonMax.x - a_buttonMin.x;
            const float buttonHeight = a_buttonMax.y - a_buttonMin.y;
            float imageWidth = buttonWidth;
            float imageHeight = imageWidth *
                static_cast<float>(a_preview.height) /
                static_cast<float>(a_preview.width);
            if (imageHeight > buttonHeight)
            {
                const float scale = buttonHeight / imageHeight;
                imageWidth *= scale;
                imageHeight = buttonHeight;
            }

            const ImVec2 imageMin(
                a_buttonMin.x + (buttonWidth - imageWidth) * 0.5f,
                a_buttonMin.y + (buttonHeight - imageHeight) * 0.5f);
            const ImVec2 imageMax(
                imageMin.x + imageWidth,
                imageMin.y + imageHeight);
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(srvGpuDescHandle.ptr),
                imageMin,
                imageMax);
        }

        Core::IO::IFileSystem* m_fileSystem = nullptr;
        AssetManager* m_assetManager = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Core::IO::Path* m_selectedAssetPath = nullptr;
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_selectedFolderPath{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_folderCache{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_fileCache{};
        std::unordered_map<std::string, TexturePreview> m_texturePreviewCache{};
        std::array<Core::IO::Path, 6> m_cubeTextureFacePaths{};
        Core::IO::Path m_cubeTextureDestinationFolder{};
        std::string m_cubeTextureStatusMessage{};
        char m_cubeTextureOutputName[128]{};
        int m_fileDrawIndex = 0;
        bool m_wasAssetSelected = false;
        bool m_shouldOpenMakeCubeTexturePopup = false;
        bool m_cubeTextureStatusIsError = false;
    };
}
